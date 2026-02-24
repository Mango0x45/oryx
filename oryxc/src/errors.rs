use std::ffi::{
	OsStr,
	OsString,
};
use std::fmt::Display;
use std::ops::Deref;
use std::path::Path;
use std::sync::OnceLock;
use std::{
	env,
	process,
};

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

pub fn err_at_position<T, S>(filename: T, s: S) -> !
where
	T: Deref<Target = OsStr>,
	S: Display,
{
	eprintln!("{}: \x1b[31;1mError:\x1b[0m {}", filename.display(), s);
	process::exit(1);
}
