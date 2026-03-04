use std::ffi::OsString;
use std::io::{
	self,
	Read,
	Write,
};
use std::iter::once;
use std::sync::atomic::{
	AtomicUsize,
	Ordering,
};
use std::sync::{
	Arc,
	OnceLock,
};
use std::{
	fs,
	process,
	thread,
};

use crossbeam_deque::{
	Injector,
	Steal,
	Stealer,
	Worker,
};
use soa_rs::Soa;

use crate::errors::OryxError;
use crate::lexer::Token;
use crate::parser::AstNode;
use crate::prelude::*;
use crate::{
	Flags,
	err,
	lexer,
	parser,
};

pub struct FileData {
	pub name:       OsString,
	pub buffer:     String,
	pub tokens:     OnceLock<Soa<Token>>,
	pub ast:        OnceLock<Soa<AstNode>>,
	pub extra_data: OnceLock<Vec<u32>>,
}

impl FileData {
	/// Read a source file from disk and create a new [`FileData`].
	fn new(name: OsString) -> Result<Self, io::Error> {
		const PAD: [u8; 64] = [0; 64]; /* 512 bits */

		// Pre-allocate to avoid reallocation when appending padding.
		// Append extra data to the end so that we can safely read past
		// instead of branching on length.
		let size = fs::metadata(&name)?.len() as usize;
		let mut buffer = String::with_capacity(size + PAD.len());
		fs::File::open(&name)?.read_to_string(&mut buffer)?;
		buffer.push_str(unsafe { std::str::from_utf8_unchecked(&PAD) });

		Ok(Self {
			name,
			buffer,
			tokens: OnceLock::new(),
			ast: OnceLock::new(),
			extra_data: OnceLock::new(),
		})
	}
}

#[allow(dead_code)]
pub enum Job {
	Lex {
		file:  FileId,
		fdata: Arc<FileData>,
	},
	Parse {
		file:  FileId,
		fdata: Arc<FileData>,
	},
	ResolveDef {
		file:  FileId,
		fdata: Arc<FileData>,
		node:  NodeId,
	},
}

pub struct CompilerState {
	#[allow(dead_code)]
	pub files:          Vec<Arc<FileData>>,
	pub globalq:        Injector<Job>,
	pub njobs:          AtomicUsize,
	pub flags:          Flags,
	pub worker_threads: OnceLock<Box<[thread::Thread]>>,
}

impl CompilerState {
	/// Unpark all worker threads.
	fn wake_all(&self) {
		if let Some(threads) = self.worker_threads.get() {
			for t in threads.iter() {
				t.unpark();
			}
		}
	}

	/// Push a job onto a worker's local queue and wake all threads.
	fn push_job(&self, queue: &Worker<Job>, job: Job) {
		self.njobs.fetch_add(1, Ordering::Relaxed);
		queue.push(job);
		self.wake_all();
	}
}

/// Initialize compiler state and drive all source files through the
/// pipeline.
pub fn start<T>(paths: T, flags: Flags)
where
	T: IntoIterator<Item = OsString>,
{
	let mut files = Vec::new();
	let mut initial_jobs = Vec::new();

	for (i, path) in paths.into_iter().enumerate() {
		let id = FileId(i);

		// take ownership of the OsString so we can store it in FileData without
		// cloning
		let display = path.to_string_lossy().into_owned();
		let fdata = Arc::new(
			FileData::new(path).unwrap_or_else(|e| err!(e, "{}", display)),
		);
		files.push(Arc::clone(&fdata));
		initial_jobs.push(Job::Lex { file: id, fdata });
	}

	let njobs = initial_jobs.len();
	let state = Arc::new(CompilerState {
		files,
		globalq: Injector::new(),
		njobs: AtomicUsize::new(njobs),
		flags,
		worker_threads: OnceLock::new(),
	});

	for job in initial_jobs {
		state.globalq.push(job);
	}

	let mut workers = Vec::with_capacity(flags.threads);
	let mut stealers = Vec::with_capacity(flags.threads);
	for _ in 0..flags.threads {
		let w = Worker::new_fifo();
		stealers.push(w.stealer());
		workers.push(w);
	}

	let stealer_view: Arc<[_]> = Arc::from(stealers);
	let handles: Vec<_> = workers
		.into_iter()
		.enumerate()
		.map(|(id, w)| {
			let stealer_view = Arc::clone(&stealer_view);
			let state = Arc::clone(&state);
			thread::spawn(move || worker_loop(id, state, w, stealer_view))
		})
		.collect();

	let worker_threads: Box<[thread::Thread]> =
		handles.iter().map(|h| h.thread().clone()).collect();
	let _ = state.worker_threads.set(worker_threads);

	// if work completes before we get here, wake them so they can observe the
	// termination condition and exit.
	state.wake_all();

	for h in handles {
		if let Err(e) = h.join() {
			std::panic::resume_unwind(e)
		}
	}
}

/// Steal and execute jobs until all work is complete.
fn worker_loop(
	_id: usize,
	state: Arc<CompilerState>,
	queue: Worker<Job>,
	stealers: Arc<[Stealer<Job>]>,
) {
	loop {
		if state.njobs.load(Ordering::Acquire) == 0 {
			break;
		}

		let Some(job) = find_task(&queue, &state.globalq, &stealers) else {
			// no work available; check termination condition before parking to avoid missed wakeups
			if state.njobs.load(Ordering::Acquire) == 0 {
				break;
			}
			thread::park();
			continue;
		};

		match job {
			Job::Lex { file, fdata } => {
				let tokens =
					lexer::tokenize(&fdata.buffer).unwrap_or_else(|e| {
						emit_errors(&fdata, once(e));
						process::exit(1)
					});

				if state.flags.debug_lexer {
					let mut handle = io::stderr().lock();
					for t in tokens.iter() {
						let _ = write!(handle, "{t:?}\n");
					}
				}

				fdata.tokens.set(tokens).unwrap();
				state.push_job(&queue, Job::Parse { file, fdata });
			},
			Job::Parse { file, fdata } => {
				let (ast, extra_data) = parser::parse(
					fdata.tokens.get().unwrap(),
				)
				.unwrap_or_else(|errs| {
					emit_errors(&fdata, errs);
					process::exit(1)
				});

				if state.flags.debug_parser {
					let mut handle = io::stderr().lock();
					for n in ast.iter() {
						let _ = write!(handle, "{n:?}\n");
					}
				}

				fdata.ast.set(ast).unwrap();
				fdata.extra_data.set(extra_data).unwrap();

				let ast = fdata.ast.get().unwrap();
				let extra_data = fdata.extra_data.get().unwrap();
				let SubNodes(i, nstmts) = ast.sub()[ast.len() - 1];

				for j in 0..nstmts {
					let node = NodeId(extra_data[(i + j) as usize]);
					let fdata = fdata.clone();
					state.push_job(
						&queue,
						Job::ResolveDef { file, fdata, node },
					);
				}
			},
			Job::ResolveDef { file, fdata, node } => {
				eprintln!("Resolving def at node index {node:?}");
			},
		}

		if state.njobs.fetch_sub(1, Ordering::Release) == 1 {
			// njobs is 0; wake all threads so they can observe the termination
			// condition and exit.
			state.wake_all();

			// break here to avoid unnecessary steal attempts after work is done.
			break;
		}
	}
}

/// Get next available job or steal from the global queue or peers if
/// local queue is empty.
fn find_task(
	localq: &Worker<Job>,
	globalq: &Injector<Job>,
	stealers: &Arc<[Stealer<Job>]>,
) -> Option<Job> {
	if let Some(job) = localq.pop() {
		return Some(job);
	}

	loop {
		match globalq.steal_batch_and_pop(localq) {
			Steal::Success(job) => return Some(job),
			Steal::Empty => break,
			Steal::Retry => continue,
		}
	}

	for s in stealers.iter() {
		loop {
			match s.steal_batch_and_pop(localq) {
				Steal::Success(job) => return Some(job),
				Steal::Empty => break,
				Steal::Retry => continue,
			}
		}
	}

	None
}

/// Print all errors to stderr using the file’s name and source buffer.
fn emit_errors<T>(fdata: &FileData, errors: T)
where
	T: IntoIterator<Item = OryxError>,
{
	for e in errors {
		e.report(&fdata.name, &fdata.buffer);
	}
}
