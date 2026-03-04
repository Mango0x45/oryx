use std::ffi::OsString;
use std::io::{
	self,
	Write,
};
use std::iter::{
	self,
	IntoIterator,
};
use std::mem::MaybeUninit;
use std::sync::Arc;
use std::sync::atomic::{
	AtomicUsize,
	Ordering,
};
use std::vec::Vec;
use std::{
	fs,
	panic,
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
	name:       Arc<OsString>,
	buffer:     Arc<String>,
	tokens:     Arc<MaybeUninit<Soa<Token>>>,
	ast:        Arc<MaybeUninit<Soa<AstNode>>>,
	extra_data: Arc<MaybeUninit<Vec<u32>>>,
}

impl FileData {
	fn new(name: OsString) -> Result<Self, io::Error> {
		const PAD: [u8; 64] = [0; 64]; /* 512 bits */

		// Append extra data to the end so that we can safely read past
		// instead of branching on length
		let mut buffer = fs::read_to_string(&name)?;
		buffer.push_str(unsafe { str::from_utf8_unchecked(&PAD) });

		return Ok(Self {
			name:       name.into(),
			buffer:     buffer.into(),
			tokens:     Arc::new_uninit(),
			ast:        Arc::new_uninit(),
			extra_data: Arc::new_uninit(),
		});
	}
}

pub enum Job {
	Lex { file: FileId },
	Parse { file: FileId },
	ResolveSymbols { file: FileId },
}

pub struct CompilerState {
	pub files:   DashMap<FileId, FileData>,
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
		let data = match FileData::new(path.clone().into()) {
			Ok(x) => x,
			Err(e) => err!(e, "{}", path.display()),
		};
		state.files.insert(id, data);
		state.njobs.fetch_add(1, Ordering::Relaxed);
		state.globalq.push(Job::Lex { file: id });
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
			worker_loop(id, state, w, stealer_view);
		}));
	}

	for t in threads {
		t.join().unwrap_or_else(|e| panic::resume_unwind(e));
	}
}

macro_rules! fdata_read {
	($state:expr, $file:expr, $($field:ident),+ $(,)?) => {
		#[allow(unused_parens)]
		let ($($field),+) = {
			let fdata = $state.files.get(&$file).unwrap();
			($(fdata.$field.clone()),+)
		};
	};
}

macro_rules! fdata_write {
	($state:expr, $file:expr, $($field:ident),+ $(,)?) => {
		{
			let mut fdata = $state.files.get_mut(&$file).unwrap();
			$(
				fdata.$field = Arc::from(MaybeUninit::new($field));
			)+
		}
	};
}

fn emit_errors<T>(state: Arc<CompilerState>, file: FileId, errors: T)
where
	T: IntoIterator<Item = OryxError>,
{
	fdata_read!(state, file, name, buffer);
	for e in errors.into_iter() {
		e.report(name.as_ref(), buffer.as_ref());
	}
}

fn worker_loop(
	id: usize,
	state: Arc<CompilerState>,
	queue: Worker<Job>,
	stealers: Arc<[Stealer<Job>]>,
) {
	loop {
		if state.njobs.load(Ordering::Relaxed) == 0 {
			break;
		}

		let job = find_task(&queue, &state.globalq, &stealers);
		if let Some(job) = job {
			match job {
				Job::Lex { file } => {
					fdata_read!(state, file, buffer);
					let tokens = match lexer::tokenize(buffer.as_ref()) {
						Ok(xs) => xs,
						Err(e) => {
							emit_errors(state.clone(), file, iter::once(e));
							process::exit(1);
						},
					};

					if state.flags.debug_lexer {
						let mut handle = io::stderr().lock();
						for t in tokens.iter() {
							let _ = write!(handle, "{t:?}\n");
						}
					}

					fdata_write!(state, file, tokens);
					state.njobs.fetch_add(1, Ordering::Relaxed);
					queue.push(Job::Parse { file });
				},
				Job::Parse { file } => {
					fdata_read!(state, file, tokens);
					let (ast, extra_data) = match parser::parse(
						unsafe { tokens.assume_init() }.as_ref(),
					) {
						Ok(xs) => xs,
						Err(errs) => {
							emit_errors(state.clone(), file, errs);
							process::exit(1);
						},
					};

					if state.flags.debug_parser {
						let mut handle = io::stderr().lock();
						for n in ast.iter() {
							let _ = write!(handle, "{n:?}\n");
						}
					}

					fdata_write!(state, file, ast, extra_data);
					state.njobs.fetch_add(1, Ordering::Relaxed);
					queue.push(Job::ResolveSymbols { file });
				},
				Job::ResolveSymbols { file } => {
					err!("not implemented");
				},
			}

			state.njobs.fetch_sub(1, Ordering::Relaxed);
		} else {
			thread::yield_now();
		}
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

	return None;
}
