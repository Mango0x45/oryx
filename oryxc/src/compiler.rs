use std::ffi::OsString;
use std::io::{
	self,
	Write,
};
use std::iter::IntoIterator;
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
	name:   Arc<OsString>,
	buffer: Arc<String>,
	tokens: Arc<MaybeUninit<Soa<Token>>>,
	ast:    Arc<MaybeUninit<Soa<AstNode>>>,
}

impl FileData {
	fn new(name: OsString) -> Result<Self, io::Error> {
		const PAD: [u8; 64] = [0; 64]; /* 512 bits */

		// Append extra data to the end so that we can safely read past
		// instead of branching on length
		let mut buffer = fs::read_to_string(&name)?;
		buffer.push_str(unsafe { str::from_utf8_unchecked(&PAD) });

		return Ok(Self {
			name:   name.into(),
			buffer: buffer.into(),
			tokens: Arc::new_uninit(),
			ast:    Arc::new_uninit(),
		});
	}
}

pub enum Job {
	LexAndParse { file: FileId },
	TypeCheck { file: FileId },
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
		state.njobs.fetch_add(1, Ordering::SeqCst);
		state.globalq.push(Job::LexAndParse { file: id });
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

fn emit_errors<T>(state: Arc<CompilerState>, file: FileId, errors: T)
where
	T: IntoIterator<Item = OryxError>,
{
	let (name, buffer) = {
		let fdata = state.files.get(&file).unwrap();
		(fdata.name.clone(), fdata.buffer.clone())
	};
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
		if state.njobs.load(Ordering::SeqCst) == 0 {
			break;
		}

		let job = find_task(&queue, &state.globalq, &stealers);
		if let Some(job) = job {
			match job {
				Job::LexAndParse { file } => {
					let (name, buffer) = {
						let fdata = state.files.get(&file).unwrap();
						(fdata.name.clone(), fdata.buffer.clone())
					};
					let (name, buffer) = (name.as_ref(), buffer.as_ref());
					let tokens = match lexer::tokenize(buffer) {
						Ok(xs) => xs,
						Err(e) => {
							emit_errors(state.clone(), file, vec![e]);
							process::exit(1);
						},
					};

					if state.flags.debug_lexer {
						let mut handle = io::stderr().lock();
						for t in tokens.iter() {
							let _ = write!(handle, "{t:?}\n");
						}
					}

					let (ast, _extra_data) = parser::parse(name, &tokens);
					let mut fdata = state.files.get_mut(&file).unwrap();
					fdata.tokens = Arc::from(MaybeUninit::new(tokens));
					fdata.ast = Arc::from(MaybeUninit::new(ast));
				},
				_ => todo!(),
			}

			state.njobs.fetch_sub(1, Ordering::SeqCst);
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
