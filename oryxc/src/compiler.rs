use std::ffi::OsString;
use std::iter::IntoIterator;
use std::sync::Arc;
use std::sync::atomic::{
	AtomicUsize,
	Ordering,
};
use std::vec::Vec;
use std::{
	panic,
	thread,
};

use crossbeam_deque::{
	Injector,
	Steal,
	Stealer,
	Worker,
};
use dashmap::DashMap;

use crate::Flags;

#[derive(Clone, Copy, Eq, Hash, PartialEq)]
pub struct FileId(u32);

pub struct FileData {
	name: OsString,
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
		let id = FileId(i as u32);
		state.files.insert(id, FileData { name: path.clone() });
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
			worker_loop(id, w, stealer_view, state);
		}));
	}

	for t in threads {
		t.join().unwrap_or_else(|e| panic::resume_unwind(e));
	}
}

fn worker_loop(
	id: usize,
	queue: Worker<Job>,
	stealers: Arc<[Stealer<Job>]>,
	state: Arc<CompilerState>,
) {
	loop {
		if state.njobs.load(Ordering::SeqCst) == 0 {
			break;
		}

		let job = find_task(&queue, &state.globalq, &stealers);
		if let Some(job) = job {
			match job {
				LexAndParse { file } => {},
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
