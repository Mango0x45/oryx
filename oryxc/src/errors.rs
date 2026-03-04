use std::borrow::Cow;
use std::convert::AsRef;
use std::error::Error;
use std::ffi::{
	OsStr,
	OsString,
};
use std::fmt::{
	self,
	Display,
	Formatter,
};
use std::io::Write;
use std::path::Path;
use std::sync::OnceLock;
use std::{
	env,
	io,
};

use unicode_width::UnicodeWidthStr;

use crate::unicode;

const TAB_AS_SPACES: &'static str = "    ";
const TABSIZE: usize = TAB_AS_SPACES.len();

#[derive(Clone, Copy, Default, Eq, PartialEq)]
pub enum ErrorStyle {
	OneLine,
	#[default]
	Standard,
}

pub static ERROR_STYLE: OnceLock<ErrorStyle> = OnceLock::new();

pub fn progname() -> &'static OsString {
	static ARGV0: OnceLock<OsString> = OnceLock::new();
	return ARGV0.get_or_init(|| {
		let default = OsStr::new("oryxc");
		let s = env::args_os().next().unwrap_or(default.into());
		return Path::new(&s).file_name().unwrap_or(default).to_os_string();
	});
}

#[macro_export]
macro_rules! warn {
	($err:expr, $fmt:literal, $($arg:tt)*) => {{
        use crate::errors::progname;
		let _ = eprintln!("{}: {}: {}", progname().display(),
            format_args!($fmt, $($arg)*), $err);
	}};

	($err:expr, $fmt:literal) => {{
		warn!($err, $fmt,);
	}};

	($err:expr) => {{
        use crate::errors::progname;
		let _ = eprintln!("{}: {}", progname().display(), $err);
	}};
}

#[macro_export]
macro_rules! err {
	($err:expr, $fmt:literal, $($arg:tt)*) => {{
        use crate::warn;
        warn!($err, $fmt, $($arg)*);
		std::process::exit(1);
	}};

	($err:expr, $fmt:literal) => {{
		err!($err, $fmt,);
	}};

	($err:expr) => {{
        use crate::warn;
        warn!($err);
		std::process::exit(1);
	}};
}

#[derive(Debug)]
pub struct OryxError {
	pub span: (usize, usize),
	pub msg:  Cow<'static, str>,
}

impl OryxError {
	pub fn new<T>(span: (usize, usize), msg: T) -> Self
	where
		T: Into<Cow<'static, str>>,
	{
		return Self {
			span,
			msg: msg.into(),
		};
	}

	pub fn report<Tf, Tb>(&self, filename: &Tf, buffer: &Tb)
	where
		Tf: AsRef<OsStr>,
		Tb: AsRef<str>,
	{
		fn nspaces(n: i32) -> i32 {
			return match () {
				() if n < 10000 => 6,
				() if n < 100000 => 7,
				() if n < 1000000 => 8,
				() if n < 10000000 => 9,
				() if n < 100000000 => 10,
				() if n < 1000000000 => 11,
				() => 12,
			};
		}

		let buffer = buffer.as_ref();
		let (mut line, mut linebeg, mut lineend) = (1, 0, buffer.len());
		for (i, c) in buffer.char_indices() {
			if unicode::line_terminator_p(c) {
				if i >= self.span.0 {
					lineend = i;
					break;
				}
				line += 1;
				linebeg = i + c.len_utf8();
			}
		}

		let (spanbeg, spanend) = (self.span.0, self.span.1.min(lineend));

		let errbeg = new_string_with_spaces(&buffer[linebeg..spanbeg]);
		let errmid = new_string_with_spaces(&buffer[spanbeg..spanend]);
		let errend = new_string_with_spaces(&buffer[spanend..lineend]);

		let errmid = if errmid.len() == 0 {
			"_".to_string()
		} else {
			errmid
		};

		/* TODO: Do tab math */
		let col = errbeg.width() + 1;

		const FNAMEBEG: &str = "\x1b[37;1m";
		const ERRORBEG: &str = "\x1b[31;1m";
		const FMTEND: &str = "\x1b[0m";

		let mut handle = io::stderr().lock();
		let _ = write!(
			handle,
			"{FNAMEBEG}{}:{line}:{col}:{FMTEND} {ERRORBEG}error:{FMTEND} {self}\n",
			filename.as_ref().display()
		);

		if *ERROR_STYLE.get_or_init(|| ErrorStyle::Standard)
			== ErrorStyle::OneLine
		{
			return;
		}

		let _ = write!(
			handle,
			" {line:>4} │ {errbeg}{ERRORBEG}{errmid}{FMTEND}{errend}\n"
		);
		for _ in 0..nspaces(line) {
			let _ = write!(handle, " ");
		}
		let _ = write!(handle, "│ ");
		for _ in 1..col {
			let _ = write!(handle, " ");
		}
		let _ = write!(handle, "{ERRORBEG}");
		for _ in 0..errmid.width().max(1) {
			let _ = write!(handle, "^");
		}
		let _ = write!(handle, "{FMTEND}\n");
	}
}

fn new_string_with_spaces(s: &str) -> String {
	let ntabs = s.bytes().filter(|b| *b == b'\t').count();
	let mut buf = String::with_capacity(s.len() + ntabs * (TABSIZE - 1));
	for c in s.chars() {
		if c == '\t' {
			buf.push_str(TAB_AS_SPACES);
		} else {
			buf.push(c);
		}
	}
	return buf;
}

impl Display for OryxError {
	fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
		return write!(f, "{}", self.msg);
	}
}

impl Error for OryxError {}
