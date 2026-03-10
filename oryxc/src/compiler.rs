use std::ffi::OsString;
use std::io::{
	self,
	Read,
	Write,
};
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
	iter,
	process,
	thread,
};

use boxcar;
use crossbeam_deque::{
	Injector,
	Steal,
	Stealer,
	Worker,
};
use dashmap::DashMap;
use soa_rs::Soa;

use crate::arena::{
	GlobalArena,
	LocalArena,
};
use crate::errors::OryxError;
use crate::intern::Interner;
use crate::lexer::Token;
use crate::parser::{
	Ast,
	AstType,
};
use crate::prelude::*;
use crate::{
	Flags,
	err,
	lexer,
	parser,
	size,
};

#[allow(dead_code)]
pub struct FileData {
	pub name:   OsString,
	pub buffer: String,
	pub tokens: OnceLock<Soa<Token>>,
	pub ast:    OnceLock<Ast>,
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

		return Ok(Self {
			name,
			buffer,
			tokens: OnceLock::new(),
			ast: OnceLock::new(),
		});
	}
}

#[allow(dead_code)]
pub enum JobType {
	Lex {
		file:  FileId,
		fdata: Arc<FileData>,
	},
	Parse {
		file:  FileId,
		fdata: Arc<FileData>,
	},
	FindSymbolsInScope {
		fdata: Arc<FileData>,
		scope: ScopeId,
		block: u32,
	},
	ResolveDefBind {
		fdata: Arc<FileData>,
		scope: ScopeId,
		node:  u32,
	},
}

pub struct Job {
	id:   usize,
	kind: JobType,
}

struct CompilerState<'a> {
	#[allow(dead_code)]
	global_arena:   GlobalArena,
	globalq:        Injector<Job>,
	njobs:          AtomicUsize,
	flags:          Flags,
	worker_threads: OnceLock<Box<[thread::Thread]>>,
	/* Files needs to be after interner, so that the files get dropped
	 * after the interner.  This is because the interner holds references
	 * to substrings of file buffers, so we want to control the drop
	 * order to avoid any potential undefined behaviour. */
	interner:       Interner<'a>,
	files:          Vec<Arc<FileData>>,
	deps:           DashMap<usize, boxcar::Vec<Job>>,
	next_id:        AtomicUsize,
	types:          boxcar::Vec<OryxType>,
}

impl<'a> CompilerState<'a> {
	/// Unpark all worker threads.
	fn wake_all(&self) {
		if let Some(threads) = self.worker_threads.get() {
			for t in threads.iter() {
				t.unpark();
			}
		}
	}

	fn job_new(&self, kind: JobType) -> Job {
		let id = self.next_id.fetch_add(1, Ordering::Relaxed);
		return Job { id, kind };
	}

	/// Push a job onto a worker's local queue and wake all threads.
	fn job_push(&self, queue: &Worker<Job>, job: Job) {
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

		// Take ownership of the OsString so we can store it in FileData
		// without cloning
		let display = path.to_string_lossy().into_owned();
		let fdata = Arc::new(
			FileData::new(path).unwrap_or_else(|e| err!(e, "{}", display)),
		);
		files.push(Arc::clone(&fdata));
		initial_jobs.push(Job {
			id:   i,
			kind: JobType::Lex { file: id, fdata },
		});
	}

	let njobs = initial_jobs.len();

	let state = Arc::new(CompilerState {
		files,
		global_arena: GlobalArena::new(size::kibibytes(64)),
		globalq: Injector::new(),
		njobs: AtomicUsize::new(njobs),
		flags,
		worker_threads: OnceLock::new(),
		interner: Interner::new(),
		deps: DashMap::new(),
		next_id: AtomicUsize::new(njobs),
		/* Temporary solution */
		types: boxcar::vec![
			OryxType::Integer /* int  */ { bits:  64, signed: true  },
			OryxType::Integer /* i8   */ { bits:   8, signed: true  },
			OryxType::Integer /* i16  */ { bits:  16, signed: true  },
			OryxType::Integer /* i32  */ { bits:  32, signed: true  },
			OryxType::Integer /* i64  */ { bits:  64, signed: true  },
			OryxType::Integer /* i128 */ { bits: 128, signed: true  },
			OryxType::Integer /* u8   */ { bits:   8, signed: false },
			OryxType::Integer /* u16  */ { bits:  16, signed: false },
			OryxType::Integer /* u32  */ { bits:  32, signed: false },
			OryxType::Integer /* u64  */ { bits:  64, signed: false },
			OryxType::Integer /* u128 */ { bits: 128, signed: false },
		],
	});

	for job in initial_jobs {
		state.globalq.push(job);
	}

	let mut workers = Box::new_uninit_slice(flags.threads);
	let mut stealers = Box::new_uninit_slice(flags.threads);
	for i in 0..flags.threads {
		let w = Worker::new_fifo();
		stealers[i].write(w.stealer());
		workers[i].write(w);
	}
	let workers = unsafe { workers.assume_init() };
	let stealers = Arc::from(unsafe { stealers.assume_init() });

	let mut worker_threads = Box::new_uninit_slice(workers.len());
	thread::scope(|s| {
		for (i, w) in workers.into_iter().enumerate() {
			let stealers = Arc::clone(&stealers);
			let state = Arc::clone(&state);
			let arena = LocalArena::new(&state.global_arena);
			let handle = s.spawn(move || worker_loop(i, state, w, stealers));
			worker_threads[i].write(handle.thread().clone());
		}
		let _ = state
			.worker_threads
			.set(unsafe { worker_threads.assume_init() });
		state.wake_all();
	});
}

/// Steal and execute jobs until all work is complete.
fn worker_loop(
	_id: usize,
	c_state: Arc<CompilerState>,
	queue: Worker<Job>,
	stealers: Arc<[Stealer<Job>]>,
) {
	let arena = LocalArena::new(&c_state.global_arena);

	loop {
		let Some(job) = find_task(&queue, &c_state.globalq, &stealers) else {
			/* No work available; check termination condition before
			 * parking to avoid missed wakeups */
			let n = c_state.njobs.load(Ordering::Acquire);
			if n == 0 {
				c_state.wake_all();
				return;
			}
			thread::park();
			continue;
		};

		match job.kind {
			JobType::Lex { file, fdata } => {
				let tokens =
					lexer::tokenize(&fdata.buffer).unwrap_or_else(|e| {
						emit_errors(&fdata, iter::once(e));
						process::exit(1)
					});

				if c_state.flags.debug_lexer {
					let mut handle = io::stderr().lock();
					for t in tokens.iter() {
						let _ = write!(handle, "{t:?}\n");
					}
				}

				fdata.tokens.set(tokens).unwrap();
				c_state.job_push(
					&queue,
					c_state.job_new(JobType::Parse { file, fdata }),
				);
			},

			JobType::Parse { file, fdata } => {
				let (ast, extra_data) = parser::parse(
					fdata.tokens.get().unwrap(),
				)
				.unwrap_or_else(|errs| {
					emit_errors(&fdata, errs);
					process::exit(1)
				});

				if c_state.flags.debug_parser {
					let mut handle = io::stderr().lock();
					for n in ast.nodes.iter() {
						let _ = write!(handle, "{n:?}\n");
					}
				}

				let root = (ast.nodes.len() - 1) as u32;
				fdata.ast.set(ast).unwrap();

				c_state.job_push(
					&queue,
					c_state.job_new(JobType::FindSymbolsInScope {
						fdata,
						scope: ScopeId::GLOBAL,
						block: root,
					}),
				);
			},

			JobType::FindSymbolsInScope {
				fdata,
				scope,
				block,
			} => {
				let tokens = fdata.tokens.get().unwrap();
				let ast = fdata.ast.get().unwrap();
				let SubNodes(beg, nstmts) = ast.nodes.sub()[block as usize];

				let mut errors = Vec::new();

				for i in beg..beg + nstmts {
					let node = ast.extra[i as usize];
					if ast.nodes.kind()[node as usize] != AstType::MultiDefBind
					{
						continue;
					}

					let identsbeg = ast.nodes.sub()[node as usize].0;
					let nidents = ast.extra[identsbeg as usize];

					for j in 0..nidents {
						let ident = ast.extra[(identsbeg + 1 + j * 2) as usize];
						let span = tokens.view()[ident as usize];

						/* Make string slice lifetime 'static */
						let view = unsafe {
							&*(&fdata.buffer[span.0..span.1] as *const str)
						};

						let symid = c_state.interner.intern(view);
						let sym = Symbol::default();

						if let Some(mut sym) =
							fdata.symtab.insert((scope, symid), sym)
						{
							sym.state = ResolutionState::Poisoned;
							fdata.symtab.insert((scope, symid), sym);

							errors.push(OryxError::new(
								span,
								format!(
									"symbol ‘{view}’ defined multiple times"
								),
							));
						}
					}

					c_state.job_push(
						&queue,
						c_state.job_new(JobType::ResolveDefBind {
							fdata: fdata.clone(),
							scope,
							node: multi_def_bind,
						}),
					);
				}

				emit_errors(&fdata, errors);
			},

			JobType::ResolveDefBind { fdata, scope, node } => {},
		}

		if let Some((_, deps)) = c_state.deps.remove(&job.id) {
			for j in deps {
				c_state.job_push(&queue, j);
			}
		}

		if c_state.njobs.fetch_sub(1, Ordering::Release) == 1 {
			// njobs is 0; wake all threads so they can observe the termination
			// condition and exit.
			c_state.wake_all();

			// break here to avoid unnecessary steal attempts after work is
			// done.
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
