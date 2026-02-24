use std::ffi::OsStr;
use std::fmt::Display;
use std::{
	iter,
	mem,
	str,
};

use phf;
use soa_rs::{
	self,
	Soars,
};

use crate::{
	errors,
	size,
	unicode,
};

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum TokenType {
	Eof         = 0,
	Ampersand   = '&' as u8,
	AngleL      = '<' as u8,
	AngleR      = '>' as u8,
	Asterisk    = '*' as u8,
	Bar         = '|' as u8,
	BraceL      = '{' as u8,
	BraceR      = '}' as u8,
	BracketL    = '[' as u8,
	BracketR    = ']' as u8,
	Caret       = '^' as u8,
	Comma       = ',' as u8,
	Equals      = '=' as u8,
	Exclamation = '!' as u8,
	Minus       = '-' as u8,
	ParenL      = '(' as u8,
	ParenR      = ')' as u8,
	Plus        = '+' as u8,
	Semicolon   = ';' as u8,
	Slash       = '/' as u8,
	Tilde       = '~' as u8,
	AmpersandTilde,
	AngleL2,
	AngleL3,
	AngleR2,
	AngleR3,
	Ellipsis,
	Identifier,
	KeywordDef,
	KeywordFunc,
	KeywordReturn,
	Number,
	String,
}

impl TokenType {
	pub fn literalp(&self) -> bool {
		return match self {
			Self::Identifier
			| Self::KeywordDef
			| Self::KeywordFunc
			| Self::Number
			| Self::String => true,
			_ => false,
		};
	}

	/* Tokens that start an expression */
	pub fn exprp(&self) -> bool {
		return match self {
			Self::Ampersand
			| Self::Caret
			| Self::Exclamation
			| Self::Identifier
			| Self::KeywordFunc
			| Self::Minus
			| Self::Number
			| Self::ParenL
			| Self::Plus
			| Self::String
			| Self::Tilde => true,
			_ => false,
		};
	}
}

#[derive(Soars)]
#[soa_derive(Debug)]
pub struct Token<'a> {
	pub kind: TokenType,
	pub view: &'a str,
}

pub struct TokenizedBuffer<'a> {
	pub tokens:   soa_rs::Soa<Token<'a>>,
	pub buffer:   &'a str,
	pub filename: Option<&'a OsStr>,
}

struct LexerContext<'a> {
	pos_a:          usize, /* Pos [a]fter char */
	pos_b:          usize, /* Pos [b]efore char */
	chars:          iter::Peekable<str::Chars<'a>>,
	string:         &'a str,
	filename:       Option<&'a OsStr>,
	expect_punct_p: bool,
}

impl<'a> LexerContext<'a> {
	fn new(filename: Option<&'a OsStr>, string: &'a str) -> Self {
		return Self {
			pos_a: 0,
			pos_b: 0,
			chars: string.chars().peekable(),
			string,
			filename,
			expect_punct_p: false,
		};
	}

	#[inline(always)]
	fn next(&mut self) -> Option<char> {
		let c = self.chars.next()?;
		self.pos_b = self.pos_a;
		self.pos_a += c.len_utf8();
		return Some(c);
	}

	#[inline(always)]
	fn peek(&mut self) -> Option<char> {
		return self.chars.peek().copied();
	}

	fn err_at_position<S>(&self, s: S) -> !
	where
		S: Display,
	{
		errors::err_at_position(self.filename.unwrap_or(OsStr::new("-")), s);
	}

	#[inline(always)]
	fn literal_spacing_guard(&self) {
		if self.expect_punct_p {
			self.err_at_position(
				"Two literals may not be directly adjacent to each other",
			);
		}
	}
}

static KEYWORDS: phf::Map<&'static str, TokenType> = phf::phf_map! {
	"def" => TokenType::KeywordDef,
	"func" => TokenType::KeywordFunc,
	"return" => TokenType::KeywordReturn,
};

pub fn tokenize<'a>(
	filename: Option<&'a OsStr>,
	s: &'a str,
) -> TokenizedBuffer<'a> {
	let mut toks = soa_rs::Soa::<Token>::with_capacity(size::kibibytes(10));
	let mut ctx = LexerContext::new(filename, s);

	while let Some(c) = ctx.next() {
		let (i, j) = (ctx.pos_b, ctx.pos_a);
		if let Some(tok) = match c {
			'/' if ctx.peek().is_some_and(|c| c == '*') => {
				skip_comment(&mut ctx);
				ctx.expect_punct_p = false;
				None
			},
			'<' if ctx.peek().is_some_and(|c| c == '<') => {
				ctx.next(); /* Consume ‘<’ */
				let kind = if ctx.peek().is_some_and(|c| c == '<') {
					ctx.next(); /* Consume ‘<’ */
					TokenType::AngleL3
				} else {
					TokenType::AngleL2
				};
				Some(Token {
					kind,
					view: &s[i..ctx.pos_a],
				})
			},
			'>' if ctx.peek().is_some_and(|c| c == '>') => {
				ctx.next(); /* Consume ‘>’ */
				let kind = if ctx.peek().is_some_and(|c| c == '>') {
					ctx.next(); /* Consume ‘>’ */
					TokenType::AngleR3
				} else {
					TokenType::AngleR2
				};
				Some(Token {
					kind,
					view: &s[i..ctx.pos_a],
				})
			},
			'&' if ctx.peek().is_some_and(|c| c == '~') => {
				ctx.next(); /* Consume ‘~’ */
				Some(Token {
					kind: TokenType::AmpersandTilde,
					view: &s[i..j + 1],
				})
			},
			'!' | '&' | '(' | ')' | '*' | '+' | ',' | '-' | '/' | ';' | '<'
			| '=' | '>' | '[' | ']' | '^' | '{' | '|' | '}' | '~' | '…' => {
				Some(Token {
					kind: unsafe { mem::transmute(c as u8) },
					view: &s[i..j],
				})
			},
			'#' => {
				ctx.literal_spacing_guard();
				Some(tokenize_number_based(&mut ctx))
			},
			'0'..='9' => {
				ctx.literal_spacing_guard();
				Some(tokenize_number(&mut ctx, "0123456789"))
			},
			'"' => {
				ctx.literal_spacing_guard();
				Some(tokenize_string(&mut ctx))
			},
			_ if unicode::xid_start_p(c) => {
				ctx.literal_spacing_guard();
				Some(tokenize_identifier(&mut ctx))
			},
			_ if unicode::pattern_white_space_p(c) => {
				if !unicode::default_ignorable_code_point_p(c) {
					ctx.expect_punct_p = false;
				}
				None
			},
			c => {
				let msg = format!("Invalid character ‘{c}’");
				ctx.err_at_position(msg.as_str());
			},
		} {
			ctx.expect_punct_p = tok.kind.literalp();
			toks.push(tok);
		}
	}

	toks.push(Token {
		kind: TokenType::Eof,
		view: &s[s.len() - 1..],
	});
	return TokenizedBuffer {
		tokens: toks,
		buffer: s,
		filename,
	};
}

fn skip_comment<'a>(ctx: &mut LexerContext<'a>) {
	ctx.next(); /* Consume ‘*’ */
	let mut depth = 1;
	while let Some(c) = ctx.next() {
		match c {
			'/' if ctx.peek().is_some_and(|c| c == '*') => {
				depth += 1;
				ctx.next(); /* Consume ‘*’ */
			},
			'*' if ctx.peek().is_some_and(|c| c == '/') => {
				depth -= 1;
				ctx.next(); /* Consume ‘/’ */
				if depth == 0 {
					return;
				}
			},
			_ => {},
		};
	}
	ctx.err_at_position("Unterminated comment");
}

fn tokenize_number_based<'a>(ctx: &mut LexerContext<'a>) -> Token<'a> {
	let i = ctx.pos_b;
	let alphabet = match ctx.next() {
		Some('b') => "01",
		Some('o') => "01234567",
		Some('d') => "0123456789",
		Some('x') => "0123456789ABCDEF",
		Some(c) => {
			let msg = format!("Invalid number base specifier ‘{c}’");
			ctx.err_at_position(msg.as_str());
		},
		None => ctx.err_at_position("Expected number base specifier after ‘#’"),
	};
	let mut tok = match ctx.next() {
		Some(c) if alphabet.contains(c) => tokenize_number(ctx, alphabet),
		Some(c) => {
			let base = match alphabet.len() {
				2 => "binary",
				8 => "octal",
				10 => "decimal",
				16 => "hexadecimal",
				_ => unreachable!(),
			};
			let msg = format!("Invalid {base} digit ‘{c}’");
			ctx.err_at_position(msg.as_str());
		},
		None => ctx.err_at_position("Expected number after base specifier"),
	};
	tok.view = &ctx.string[i..ctx.pos_a];
	return tok;
}

fn tokenize_number<'a>(
	ctx: &mut LexerContext<'a>,
	alphabet: &'static str,
) -> Token<'a> {
	let i = ctx.pos_b;
	span_raw_number(ctx, alphabet, true);

	/* Fractional part */
	if ctx.peek().is_some_and(|c| c == '.') {
		ctx.next();
		if ctx.peek().is_some_and(|c| alphabet.contains(c)) {
			span_raw_number(ctx, alphabet, false);
		}
	}

	/* Exponential part */
	if ctx.peek().is_some_and(|c| c == 'e') {
		ctx.next();
		span_raw_number(ctx, alphabet, false);
	}

	return Token {
		kind: TokenType::Number,
		view: &ctx.string[i..ctx.pos_a],
	};
}

fn span_raw_number<'a>(
	ctx: &mut LexerContext<'a>,
	alphabet: &'static str,
	first_digit_lexed_p: bool,
) {
	if !first_digit_lexed_p {
		match ctx.next() {
			Some(c) if alphabet.contains(c) => c,
			Some(c) => {
				let base = match alphabet.len() {
					2 => "binary",
					8 => "octal",
					10 => "decimal",
					16 => "hexadecimal",
					_ => unreachable!(),
				};
				let msg = format!("Invalid {base} digit ‘{c}’");
				ctx.err_at_position(msg.as_str());
			},
			None => {
				let base = match alphabet.len() {
					2 => "binary",
					8 => "octal",
					10 => "decimal",
					16 => "hexadecimal",
					_ => unreachable!(),
				};
				let msg = format!(
					"Expected {base} digit but reached end-of-file instead"
				);
				ctx.err_at_position(msg.as_str());
			},
		};
	}

	let mut last_was_apos_p = false;
	while let Some(c) = ctx.peek() {
		match c {
			'\'' if last_was_apos_p => ctx.err_at_position(
				"Multiple concurrent digit separators in numeric literal",
			),
			'\'' => {
				last_was_apos_p = true;
				ctx.next();
			},
			_ if alphabet.contains(c) => {
				last_was_apos_p = false;
				ctx.next();
			},
			_ => break,
		};
	}

	if last_was_apos_p {
		ctx.err_at_position(
			"Numeric literals may not end with a digit separator",
		);
	}
}

fn tokenize_string<'a>(ctx: &mut LexerContext<'a>) -> Token<'a> {
	let i = ctx.pos_b;
	loop {
		if let Some(c) = ctx.next() {
			if c == '"' {
				break;
			}
		} else {
			ctx.err_at_position("Unterminated string");
		}
	}
	return Token {
		kind: TokenType::String,
		view: &ctx.string[i..ctx.pos_a],
	};
}

fn tokenize_identifier<'a>(ctx: &mut LexerContext<'a>) -> Token<'a> {
	let i = ctx.pos_b;
	while ctx.peek().is_some_and(unicode::xid_continue_p) {
		ctx.next();
	}
	let view = &ctx.string[i..ctx.pos_a];
	let kind = match KEYWORDS.get(view) {
		Some(kind) => kind.clone(),
		None => TokenType::Identifier,
	};
	return Token { kind, view };
}
