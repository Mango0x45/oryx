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

use boxcar;
use crossbeam_deque::{
	Injector,
	Steal,
	Stealer,
	Worker,
};
use dashmap::DashMap;
use soa_rs::Soa;

use crate::errors::OryxError;
use crate::intern::Interner;
use crate::lexer::Token;
use crate::parser::{
	AstNode,
	AstType,
};
use crate::prelude::*;
use crate::{
	Flags,
	err,
	lexer,
	parser,
};

#[allow(dead_code)]
pub struct FileData {
	pub name:       OsString,
	pub buffer:     String,
	pub tokens:     OnceLock<Soa<Token>>,
	pub ast:        OnceLock<Soa<AstNode>>,
	pub extra_data: OnceLock<Vec<u32>>,
	pub symtab:     DashMap<(ScopeId, SymbolId), Symbol>,
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
			symtab: DashMap::new(),
		})
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

pub struct CompilerState<'a> {
	#[allow(dead_code)]
	pub globalq:        Injector<Job>,
	pub njobs:          AtomicUsize,
	pub flags:          Flags,
	pub worker_threads: OnceLock<Box<[thread::Thread]>>,
	/* Files needs to be after interner, so that the files get dropped
	 * after the interner.  This is because the interner holds references
	 * to substrings of file buffers, so we want to control the drop
	 * order to avoid any potential undefined behaviour. */

	interner: Interner<'a>,
	files:    Vec<Arc<FileData>>,

	deps:    DashMap<usize, boxcar::Vec<Job>>,
	next_id: AtomicUsize,
	types:   boxcar::Vec<OryxType>,
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

		// take ownership of the OsString so we can store it in FileData without
		// cloning
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

	// if work completes before we get here, wake them so they can observe
	// the termination condition and exit.
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
		let Some(job) = find_task(&queue, &state.globalq, &stealers) else {
			/* No work available; check termination condition before
			 * parking to avoid missed wakeups */
			if state.njobs.load(Ordering::Acquire) == 0 {
				state.wake_all();
				return;
			}
			thread::park();
			continue;
		};

		match job.kind {
			JobType::Lex { file, fdata } => {
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
				state.job_push(
					&queue,
					state.job_new(JobType::Parse { file, fdata }),
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

				if state.flags.debug_parser {
					let mut handle = io::stderr().lock();
					for n in ast.iter() {
						let _ = write!(handle, "{n:?}\n");
					}
				}

				let root = (ast.len() - 1) as u32;
				fdata.ast.set(ast).unwrap();
				fdata.extra_data.set(extra_data).unwrap();

				state.job_push(
					&queue,
					state.job_new(JobType::FindSymbolsInScope {
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
				let extra_data = fdata.extra_data.get().unwrap();
				let SubNodes(beg, nstmts) = ast.sub()[block as usize];

				let mut errors = Vec::new();

				for i in beg..beg + nstmts {
					let multi_def_bind = extra_data[i as usize];

					if ast.kind()[multi_def_bind as usize]
						!= AstType::MultiDefBind
					{
						continue;
					}

					let def_idents = ast.sub()[multi_def_bind as usize].0;
					let nidents = extra_data[def_idents as usize];

					for j in 0..nidents {
						let ident =
							extra_data[(def_idents + 1 + j * 2) as usize];
						let span = tokens.view()[ident as usize];

						/* Make string slice lifetime 'static */
						let view = unsafe {
							&*(&fdata.buffer[span.0..span.1] as *const str)
						};

						let symid = state.interner.intern(view);
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

					state.job_push(
						&queue,
						state.job_new(JobType::ResolveDefBind {
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

		if let Some((_, deps)) = state.deps.remove(&job.id) {
			for j in deps {
				state.job_push(&queue, j);
			}
		}

		if state.njobs.fetch_sub(1, Ordering::Release) == 1 {
			// njobs is 0; wake all threads so they can observe the termination
			// condition and exit.
			state.wake_all();

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
