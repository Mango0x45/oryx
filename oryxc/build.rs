const NAMES: &[&str] = &[
	"xid_start",
	"xid_continue",
	"pattern_white_space",
	"line_terminator",
];

fn main() {
	use std::env;

	let out_dir = env::var("OUT_DIR").unwrap();
	let root = env::var("CARGO_MANIFEST_DIR").unwrap();
	let generated = format!("{root}/generated");

	#[cfg(feature = "fetch")]
	fetch::run(&out_dir, &generated);

	#[cfg(not(feature = "fetch"))]
	fallback::run(&out_dir, &generated);
}

#[cfg(feature = "fetch")]
mod fetch {
	use std::collections::HashMap;
	use std::fs;
	use std::io::{
		self,
		BufRead,
		BufReader,
		Cursor,
		Read,
		Write,
	};

	const MIN_SHIFT: usize = 1;
	const MAX_SHIFT: usize = 22;
	const UCD_URL: &str =
		"https://www.unicode.org/Public/zipped/latest/UCD.zip";

	pub fn run(out_dir: &str, generated: &str) {
		let data = format!("{out_dir}/data");
		let derived = format!("{data}/DerivedCoreProperties.txt");
		let proplist = format!("{data}/PropList.txt");

		println!("cargo:rerun-if-changed={derived}");
		println!("cargo:rerun-if-changed={proplist}");

		if !fs::exists(&derived).unwrap_or(false)
			|| !fs::exists(&proplist).unwrap_or(false)
		{
			let mut bytes = Vec::new();
			ureq::get(UCD_URL)
				.call()
				.expect("failed to download UCD.zip")
				.into_reader()
				.read_to_end(&mut bytes)
				.expect("failed to read UCD.zip");

			fs::create_dir_all(&data).unwrap();
			zip::ZipArchive::new(Cursor::new(bytes))
				.expect("failed to open UCD.zip")
				.extract(&data)
				.expect("failed to extract UCD.zip");

			// XID_Start and XID_Continue additions
			let mut f = fs::OpenOptions::new()
				.append(true)
				.open(&derived)
				.expect("failed to open DerivedCoreProperties.txt");
			writeln!(
				f,
				"0024          ; XID_Start # Pc       DOLLAR SIGN\n\
				 005F          ; XID_Start # Pc       LOW LINE\n\
				 2032..2034    ; XID_Continue # Po   [3] PRIME..TRIPLE PRIME\n\
				 2057          ; XID_Continue # Po       QUADRUPLE PRIME"
			)
			.unwrap();
		}

		generate_from_file(out_dir, &derived, "XID_Start", "xid_start");
		generate_from_file(out_dir, &derived, "XID_Continue", "xid_continue");
		generate_from_file(
			out_dir,
			&proplist,
			"Pattern_White_Space",
			"pattern_white_space",
		);
		generate_from_codepoints(
			out_dir,
			&[
				'\u{A}', '\u{B}', '\u{C}', '\u{D}', '\u{85}', '\u{2028}',
				'\u{2029}',
			],
			"line_terminator",
		);

		// Keep generated/ in sync so it can be committed as a fallback
		fs::create_dir_all(generated).unwrap();
		for name in super::NAMES {
			fs::copy(
				format!("{out_dir}/{name}.rs"),
				format!("{generated}/{name}.rs"),
			)
			.unwrap_or_else(|e| {
				panic!("failed to copy {name}.rs to generated/: {e}")
			});
		}
	}

	fn generate_from_file(out_dir: &str, path: &str, prop: &str, name: &str) {
		let mut bitmap = vec![false; 0x110000];
		parse_file(path, prop, &mut bitmap)
			.unwrap_or_else(|e| panic!("failed to read {path}: {e}"));
		write_output(out_dir, name, &bitmap);
	}

	fn generate_from_codepoints(
		out_dir: &str,
		codepoints: &[char],
		name: &str,
	) {
		let mut bitmap = vec![false; 0x110000];
		for &c in codepoints {
			bitmap[c as usize] = true;
		}
		write_output(out_dir, name, &bitmap);
	}

	fn write_output(out_dir: &str, name: &str, bitmap: &[bool]) {
		let (shift, lvl1, lvl2) = optimize_tables(bitmap);
		let mut f = fs::File::create(format!("{out_dir}/{name}.rs")).unwrap();
		generate_code(&mut f, name, shift, &lvl1, &lvl2);
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

		config
	}

	fn parse_file(
		path: &str,
		prop: &str,
		bitmap: &mut [bool],
	) -> io::Result<()> {
		let file = fs::File::open(path)?;
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

		Ok(())
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

		(lvl1, lvl2)
	}

	fn generate_code(
		f: &mut impl Write,
		prop_name: &str,
		shift: usize,
		level1: &[u16],
		level2: &[u64],
	) {
		let upper_name = prop_name.to_uppercase();
		let lower_name = prop_name.to_lowercase();
		let block_size = 1 << shift;
		let mask = block_size - 1;
		let u64s_per_block = (block_size + 63) / 64;

		let pred_name = if lower_name.contains('_') {
			format!("{lower_name}_p")
		} else {
			format!("{lower_name}p")
		};

		writeln!(f, "/* Autogenerated – DO NOT EDIT */").unwrap();
		writeln!(f).unwrap();
		writeln!(
			f,
			"static {upper_name}_L1: [u16; {}] = {level1:?};",
			level1.len()
		)
		.unwrap();
		writeln!(
			f,
			"static {upper_name}_L2: [u64; {}] = {level2:?};",
			level2.len()
		)
		.unwrap();
		writeln!(f, "#[inline]").unwrap();
		writeln!(f, "pub fn {pred_name}(c: char) -> bool {{").unwrap();
		writeln!(f, "\tlet cp = c as usize;").unwrap();
		writeln!(f, "\tlet blki = unsafe {{ *{upper_name}_L1.get_unchecked(cp >> {shift}) }} as usize;").unwrap();
		writeln!(f, "\tlet in_blk_offset_p = cp & 0x{mask:X};").unwrap();
		if u64s_per_block == 1 {
			writeln!(f, "\tunsafe {{ return ({upper_name}_L2.get_unchecked(blki) & (1 << in_blk_offset_p)) != 0; }}").unwrap();
		} else {
			writeln!(
				f,
				"\tlet wordi = (blki * {u64s_per_block}) + (in_blk_offset_p >> 6);"
			)
			.unwrap();
			writeln!(f, "\tlet biti = in_blk_offset_p & 0x3F;").unwrap();
			writeln!(f, "\tunsafe {{ return (*{upper_name}_L2.get_unchecked(wordi) & (1 << biti)) != 0; }}").unwrap();
		}
		writeln!(f, "}}").unwrap();
	}
}

#[cfg(not(feature = "fetch"))]
mod fallback {
	use std::fs;

	pub fn run(out_dir: &str, generated: &str) {
		for name in super::NAMES {
			let src = format!("{generated}/{name}.rs");
			println!("cargo:rerun-if-changed={src}");
			fs::copy(&src, format!("{out_dir}/{name}.rs"))
				.unwrap_or_else(|e| panic!("failed to copy {src}: {e}"));
		}
	}
}
