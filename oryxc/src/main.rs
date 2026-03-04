#![allow(unsafe_op_in_unsafe_fn)]

mod compiler;
mod errors;
mod intern;
mod lexer;
mod parser;
mod prelude;
mod size;
mod unicode;

use std::ffi::OsString;
use std::thread;

use clap::{
	CommandFactory,
	FromArgMatches,
	Parser,
};

#[derive(Clone, Copy, Default)]
pub struct Flags {
	pub debug_lexer:  bool,
	pub debug_parser: bool,
	pub threads:      usize,
	pub error_style:  errors::ErrorStyle,
}

#[derive(Parser)]
struct Args {
	#[arg(short = 'l', long)]
	debug_lexer: bool,

	#[arg(short = 'p', long)]
	debug_parser: bool,

	#[arg(short = 's', long, default_value = "standard")]
	error_style: errors::ErrorStyle,

	#[arg(short = 't', long)]
	threads: Option<usize>,

	files: Vec<OsString>,
}

fn main() {
	let args = Args::from_arg_matches(
		&Args::command().override_usage(usage()).get_matches(),
	)
	.unwrap_or_else(|e| e.exit());

	let threads = args.threads.unwrap_or_else(|| {
		thread::available_parallelism().map_or_else(
			|e| {
				warn!(e, "failed to get thread count");
				1
			},
			|x| x.get(),
		)
	});

	if threads == 0 {
		err!("thread count must be greater than 0");
	}

	let flags = Flags {
		debug_lexer: args.debug_lexer,
		debug_parser: args.debug_parser,
		threads,
		error_style: args.error_style,
	};

	let _ = errors::ERROR_STYLE.set(flags.error_style);
	compiler::start(args.files, flags);
}

fn usage() -> String {
	format!(
		concat!(
			"Usage: {0} [-lp] [-s oneline|standard] [-t threads]\n",
			"       {0} -h",
		),
		errors::progname().display()
	)
}
