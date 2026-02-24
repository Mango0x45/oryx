use std::collections::HashMap;
use std::ffi::{
	OsStr,
	OsString,
};
use std::fs::File;
use std::io::{
	self,
	BufRead,
	BufReader,
};
use std::path::Path;
use std::sync::OnceLock;
use std::vec::Vec;
use std::{
	env,
	process,
};

const MIN_SHIFT: usize = 1;
const MAX_SHIFT: usize = 22;

#[derive(Default)]
struct Flags {
	codepoints: Option<Vec<char>>,
	help:       bool,
}

impl Flags {
	fn parse() -> Result<(Flags, Vec<String>), lexopt::Error> {
		use lexopt::prelude::*;

		let mut rest = Vec::with_capacity(env::args().len() - 1);
		let mut flags = Flags::default();
		let mut parser = lexopt::Parser::from_env();

		while let Some(arg) = parser.next()? {
			match arg {
				Short('c') | Long("codepoints") => {
					fn hex_to_char(s: &str) -> char {
						return u32::from_str_radix(s, 16).map_or_else(
							|e| {
								eprintln!("{}: {s}: {e}", progname().display());
								process::exit(1);
							},
							|n| {
								char::from_u32(n).unwrap_or_else(|| {
									eprintln!(
										"{}: {s}: invalid codepoint",
										progname().display()
									);
									process::exit(1);
								})
							},
						);
					}

					flags.codepoints = Some(
						parser
							.value()?
							.to_str()
							.unwrap_or_else(|| {
								eprintln!(
									"{}: unable to parse argument to -c/--codepoints",
									progname().display()
								);
								process::exit(1);
							})
							.split(',')
							.map(hex_to_char)
							.collect(),
					);
				},
				Short('h') | Long("help") => flags.help = true,
				Value(v) => rest.push(v.into_string()?),
				_ => return Err(arg.unexpected()),
			}
		}

		return Ok((flags, rest));
	}
}

fn progname() -> &'static OsString {
	static ARGV0: OnceLock<OsString> = OnceLock::new();
	return ARGV0.get_or_init(|| {
		let default = OsStr::new("oryxc");
		let s = env::args_os().next().unwrap_or(default.into());
		return Path::new(&s).file_name().unwrap_or(default).to_os_string();
	});
}

fn usage() {
	eprintln!(
		concat!(
			"Usage: {0} data-file property-name\n",
			"       {0} -c codepoints name\n",
			"       {0} -h",
		),
		progname().display()
	);
}

fn main() -> io::Result<()> {
	let (flags, rest) = match Flags::parse() {
		Ok(v) => v,
		Err(e) => {
			eprintln!("{}: {e}", progname().display());
			usage();
			process::exit(1);
		},
	};

	if flags.help {
		usage();
		process::exit(0);
	}

	if (flags.codepoints.is_none() && rest.len() != 2)
		|| (flags.codepoints.is_some() && rest.len() != 1)
	{
		usage();
		process::exit(1);
	}

	let mut bitmap = vec![false; 0x110000];
	let name = match flags.codepoints {
		Some(vec) => {
			vec.iter().for_each(|c| bitmap[*c as usize] = true);
			&rest[0]
		},
		None => {
			parse_file(&rest[0], &rest[1], &mut bitmap)?;
			&rest[1]
		},
	};
	let (shift, lvl1, lvl2) = optimize_tables(&bitmap);
	write_tables(name, shift, &lvl1, &lvl2);
	return Ok(());
}

fn optimize_tables(bitmap: &[bool]) -> (usize, Vec<u16>, Vec<u64>) {
	let mut minsz = usize::MAX;
	let mut config = (0, Vec::new(), Vec::new());

	for i in MIN_SHIFT..=MAX_SHIFT {
		let (l1, l2) = build_tables(bitmap, i);
		let sz = l1.len() * 2 + l2.len() * 8;
		if sz < minsz {
			minsz = sz;
			config = (i, l1, l2);
		}
	}

	return config;
}

fn parse_file<P: AsRef<Path>>(
	path: P,
	prop: &str,
	bitmap: &mut [bool],
) -> io::Result<()> {
	let file = File::open(path)?;
	let reader = BufReader::new(file);

	for line in reader.lines() {
		let line = line?;
		let line = line.split('#').next().unwrap_or("").trim();
		if line.is_empty() {
			continue;
		}

		let parts: Vec<&str> = line.split(';').map(|s| s.trim()).collect();
		if parts.len() < 2 || parts[1] != prop {
			continue;
		}

		let (beg, end) = if parts[0].contains("..") {
			let mut range = parts[0].split("..");
			(
				u32::from_str_radix(range.next().unwrap(), 16).unwrap(),
				u32::from_str_radix(range.next().unwrap(), 16).unwrap(),
			)
		} else {
			let val = u32::from_str_radix(parts[0], 16).unwrap();
			(val, val)
		};

		for cp in beg..=end {
			if (cp as usize) < bitmap.len() {
				bitmap[cp as usize] = true;
			}
		}
	}
	return Ok(());
}

fn build_tables(bitmap: &[bool], shift: usize) -> (Vec<u16>, Vec<u64>) {
	let blksz = 1 << shift;
	let u64s_per_block = (blksz + 63) / 64;

	let mut lvl2: Vec<u64> = Vec::new();
	let mut lvl1: Vec<u16> = Vec::new();
	let mut blkmap: HashMap<Vec<u64>, u16> = HashMap::new();

	for chunk in bitmap.chunks(blksz) {
		let mut blkdata = vec![0u64; u64s_per_block];

		for (i, &bit) in chunk.iter().enumerate() {
			if bit {
				let word_idx = i / 64;
				let bit_idx = i % 64;
				blkdata[word_idx] |= 1 << bit_idx;
			}
		}

		if let Some(&i) = blkmap.get(&blkdata) {
			lvl1.push(i);
		} else {
			let i = (lvl2.len() / u64s_per_block) as u16;
			lvl2.extend_from_slice(&blkdata);
			blkmap.insert(blkdata, i);
			lvl1.push(i);
		}
	}

	return (lvl1, lvl2);
}

fn write_tables(prop_name: &str, shift: usize, level1: &[u16], level2: &[u64]) {
	let upper_name = prop_name.to_uppercase();
	let lower_name = prop_name.to_lowercase();
	let block_size = 1 << shift;
	let mask = block_size - 1;
	let u64s_per_block = (block_size + 63) / 64;

	println!("/* Autogenerated – DO NOT EDIT */\n");
	print!(
		"static {upper_name}_L1: [u16; {}] = {level1:?};",
		level1.len()
	);
	print!(
		"static {upper_name}_L2: [u64; {}] = {level2:?};",
		level2.len()
	);

	let pred_name = if lower_name.contains('_') {
		format!("{lower_name}_p")
	} else {
		format!("{lower_name}p")
	};

	print!(
		"#[inline]
		pub fn {pred_name}(c: char) -> bool {{
			let cp = c as usize;
			let blki = unsafe {{ *{upper_name}_L1.get_unchecked(cp >> {shift}) }} as usize;
			let in_blk_offset_p = cp & 0x{mask:X};"
	);

	if u64s_per_block == 1 {
		print!(
			"	unsafe {{
					return ({upper_name}_L2.get_unchecked(blki) & (1 << in_blk_offset_p)) != 0;
				}}"
		);
	} else {
		print!(
			"let wordi = (blki * {u64s_per_block}) + (in_blk_offset_p >> 6);
			let biti = in_blk_offset_p & 0x3F;
			unsafe {{
				return (*{upper_name}_L2.get_unchecked(wordi) & (1 << biti)) != 0;
			}}"
		);
	}

	print!("}}");
}
