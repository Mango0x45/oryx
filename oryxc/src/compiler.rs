use std::ffi::OsString;
use std::io::{
	self,
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
use dashmap::DashMap;
use soa_rs::Soa;

use crate::errors::OryxError;
use crate::lexer::Token;
use crate::parser::AstNode;
use crate::{
	Flags,
	err,
	lexer,
	parser,
};

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct FileId(usize);

pub struct FileData {
	pub name:       OsString,
	pub buffer:     String,
	pub tokens:     OnceLock<Soa<Token>>,
	pub ast:        OnceLock<Soa<AstNode>>,
	pub extra_data: OnceLock<Vec<u32>>,
}

impl FileData {
	fn new(name: OsString) -> Result<Self, io::Error> {
		const PAD: [u8; 64] = [0; 64]; /* 512 bits */

		// Append extra data to the end so that we can safely read past
		// instead of branching on length
		let mut buffer = fs::read_to_string(&name)?;
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
	Lex { file: FileId, fdata: Arc<FileData> },
	Parse { file: FileId, fdata: Arc<FileData> },
	ResolveSymbols { file: FileId, fdata: Arc<FileData> },
}

pub struct CompilerState {
	pub files:   DashMap<FileId, Arc<FileData>>,
	pub globalq: Injector<Job>,
	pub njobs:   AtomicUsize,
	pub flags:   Flags,
}

pub fn start<T>(paths: T, flags: Flags)
where
	T: IntoIterator<Item = OsString>,
{
	let state = Arc::new(CompilerState {
		files: DashMap::new(),
		globalq: Injector::new(),
		njobs: AtomicUsize::new(0),
		flags,
	});
	for (i, path) in paths.into_iter().enumerate() {
		let id = FileId(i);
		let fdata = Arc::new(
			FileData::new(path.clone().into())
				.unwrap_or_else(|e| err!(e, "{}", path.display())),
		);
		state.files.insert(id, Arc::clone(&fdata));
		state.njobs.fetch_add(1, Ordering::Relaxed);
		state.globalq.push(Job::Lex { file: id, fdata });
	}

	let mut workers = Vec::with_capacity(flags.threads);
	let mut stealers = Vec::with_capacity(flags.threads);
	for _ in 0..flags.threads {
		let w = Worker::new_fifo();
		stealers.push(w.stealer());
		workers.push(w);
	}

	let mut threads = Vec::with_capacity(flags.threads);
	let stealer_view: Arc<[_]> = Arc::from(stealers);

	for (id, w) in workers.into_iter().enumerate() {
		let stealer_view = Arc::clone(&stealer_view);
		let state = Arc::clone(&state);
		threads.push(thread::spawn(move || {
			worker_loop(id, state, w, stealer_view)
		}));
	}

	for t in threads {
		if let Err(e) = t.join() {
			std::panic::resume_unwind(e)
		}
	}
}

fn emit_errors<T>(fdata: &FileData, errors: T)
where
	T: IntoIterator<Item = OryxError>,
{
	for e in errors {
		e.report(&fdata.name, &fdata.buffer);
	}
}

fn worker_loop(
	_id: usize,
	state: Arc<CompilerState>,
	queue: Worker<Job>,
	stealers: Arc<[Stealer<Job>]>,
) {
	loop {
		if state.njobs.load(Ordering::Relaxed) == 0 {
			break;
		}

		let Some(job) = find_task(&queue, &state.globalq, &stealers) else {
			thread::yield_now();
			continue;
		};

		match job {
			Job::Lex { file, fdata } => {
				let tokens = match lexer::tokenize(&fdata.buffer) {
					Ok(xs) => xs,
					Err(e) => {
						emit_errors(&fdata, once(e));
						process::exit(1)
					},
				};

				if state.flags.debug_lexer {
					let mut handle = io::stderr().lock();
					for t in tokens.iter() {
						let _ = write!(handle, "{t:?}\n");
					}
				}

				fdata.tokens.set(tokens).unwrap();
				state.njobs.fetch_add(1, Ordering::Relaxed);
				queue.push(Job::Parse { file, fdata });
			},
			Job::Parse { file, fdata } => {
				let (ast, extra_data) =
					match parser::parse(fdata.tokens.get().unwrap()) {
						Ok(xs) => xs,
						Err(errs) => {
							emit_errors(&fdata, errs);
							process::exit(1)
						},
					};

				if state.flags.debug_parser {
					let mut handle = io::stderr().lock();
					for n in ast.iter() {
						let _ = write!(handle, "{n:?}\n");
					}
				}

				fdata.ast.set(ast).unwrap();
				fdata.extra_data.set(extra_data).unwrap();
				state.njobs.fetch_add(1, Ordering::Relaxed);
				queue.push(Job::ResolveSymbols { file, fdata });
			},
			Job::ResolveSymbols { file: _, fdata: _ } => {
				err!("not implemented");
				// unimplemented!()
			},
		}

		state.njobs.fetch_sub(1, Ordering::Relaxed);
	}
}

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
