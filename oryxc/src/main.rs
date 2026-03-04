#![allow(unsafe_op_in_unsafe_fn)]

mod compiler;
mod errors;
mod lexer;
mod parser;
mod size;
mod unicode;

use std::ffi::OsString;
use std::{
	env,
	process,
	thread,
};

use lexopt;

#[derive(Clone, Copy, Default)]
pub struct Flags {
	pub debug_lexer:  bool,
	pub debug_parser: bool,
	pub help:         bool,
	pub threads:      usize,
	pub error_style:  errors::ErrorStyle,
}

impl Flags {
	fn parse() -> Result<(Flags, Vec<OsString>), lexopt::Error> {
		use lexopt::prelude::*;

		let mut rest = Vec::with_capacity(env::args().len());
		let mut flags = Flags::default();
		let mut parser = lexopt::Parser::from_env();
		parser.set_short_equals(false);

		while let Some(arg) = parser.next()? {
			match arg {
				Short('h') | Long("help") => flags.help = true,
				Short('l') | Long("debug-lexer") => flags.debug_lexer = true,
				Short('p') | Long("debug-parser") => flags.debug_parser = true,
				Short('s') | Long("error-style") => {
					/* TODO: Don’t unwrap */
					flags.error_style = match parser.value()?.to_str().unwrap() {
						"oneline" => errors::ErrorStyle::OneLine,
						"standard" => errors::ErrorStyle::Standard,
						s => Err(format!("{s}: invalid value for -s/--error-style"))?,
					};
				},
				Short('t') | Long("threads") => {
					flags.threads = parser.value()?.parse()?;
					if flags.threads == 0 {
						err!("thread count must be greater than 0");
					}
				},
				Value(v) => rest.push(v),
				_ => return Err(arg.unexpected()),
			}
		}

		if flags.threads == 0 {
			flags.threads = thread::available_parallelism().map_or_else(
				|e| {
					warn!(e, "failed to get thread count");
					1
				},
				|x| x.get(),
			);
		}

		return Ok((flags, rest));
	}
}

fn usage() {
	eprintln!(
		concat!(
			"Usage: {0} [-lp] [-s oneline|standard] [-t threads]\n",
			"       {0} -h",
		),
		errors::progname().display()
	);
}

fn main() {
	let (flags, rest) = match Flags::parse() {
		Ok(v) => v,
		Err(e) => {
			warn!(e);
			usage();
			process::exit(1);
		},
	};

	if flags.help {
		usage();
		process::exit(0);
	}

	let _ = errors::ERROR_STYLE.set(flags.error_style);
	compiler::start(rest, flags);
}
