use std::iter::Peekable;
use std::mem;
use std::str::{
	self,
	Chars,
};

use phf;
use soa_rs::{
	Soa,
	Soars,
};

use crate::errors::OryxError;
use crate::unicode;

#[allow(dead_code)]
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

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Span(usize, usize);

#[derive(Soars)]
#[soa_derive(Debug)]
pub struct Token {
	pub kind: TokenType,
	pub view: Span,
}

struct LexerContext<'a> {
	pos_a:  usize, /* Pos [a]fter char */
	pos_b:  usize, /* Pos [b]efore char */
	chars:  Peekable<Chars<'a>>,
	string: &'a str,
}

impl<'a> LexerContext<'a> {
	fn new(string: &'a str) -> Self {
		return Self {
			pos_a: 0,
			pos_b: 0,
			chars: string.chars().peekable(),
			string,
		};
	}

	#[inline(always)]
	fn next(&mut self) -> Option<char> {
		let c = self.chars.next()?;
		self.pos_b = self.pos_a;
		self.pos_a += c.len_utf8();
		return if c == '\0' { None } else { Some(c) };
	}

	#[inline(always)]
	fn peek(&mut self) -> Option<char> {
		return self.chars.peek().copied();
	}
}

static KEYWORDS: phf::Map<&'static str, TokenType> = phf::phf_map! {
	"def" => TokenType::KeywordDef,
	"func" => TokenType::KeywordFunc,
	"return" => TokenType::KeywordReturn,
};

pub fn tokenize(s: &str) -> Result<Soa<Token>, OryxError> {
	let mut toks = Soa::<Token>::with_capacity(s.len() / 2);
	let mut ctx = LexerContext::new(s);

	while let Some(c) = ctx.next() {
		let (i, j) = (ctx.pos_b, ctx.pos_a);
		if let Some(tok) = match c {
			'/' if ctx.peek().is_some_and(|c| c == '*') => {
				skip_comment(&mut ctx)?;
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
					view: Span(i, ctx.pos_a),
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
					view: Span(i, ctx.pos_a),
				})
			},
			'&' if ctx.peek().is_some_and(|c| c == '~') => {
				ctx.next(); /* Consume ‘~’ */
				Some(Token {
					kind: TokenType::AmpersandTilde,
					view: Span(i, j + 1),
				})
			},
			'!' | '&' | '(' | ')' | '*' | '+' | ',' | '-' | '/' | ';' | '<'
			| '=' | '>' | '[' | ']' | '^' | '{' | '|' | '}' | '~' | '…' => {
				Some(Token {
					kind: unsafe { mem::transmute(c as u8) },
					view: Span(i, j),
				})
			},
			'#' => Some(tokenize_number_based(&mut ctx)?),
			'0'..='9' => Some(tokenize_number(&mut ctx, "0123456789")?),
			'"' => Some(tokenize_string(&mut ctx)?),
			_ if unicode::xid_start_p(c) => Some(tokenize_identifier(&mut ctx)),
			_ if unicode::pattern_white_space_p(c) => None,
			c => {
				return Err(OryxError::new(
					i,
					j,
					format!("Invalid character ‘{c}’"),
				));
			},
		} {
			toks.push(tok);
		}
	}

	toks.push(Token {
		kind: TokenType::Eof,
		view: Span(s.len() - 1, s.len()),
	});
	return Ok(toks);
}

fn skip_comment<'a>(ctx: &mut LexerContext<'a>) -> Result<(), OryxError> {
	let beg = ctx.pos_b;
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
					return Ok(());
				}
			},
			_ => {},
		};
	}
	return Err(OryxError::new(beg, ctx.pos_a, "Unterminated comment"));
}

fn tokenize_number_based<'a>(
	ctx: &mut LexerContext<'a>,
) -> Result<Token, OryxError> {
	let i = ctx.pos_b;
	let alphabet = match ctx.next() {
		Some('b') => "01",
		Some('o') => "01234567",
		Some('d') => "0123456789",
		Some('x') => "0123456789ABCDEF",
		Some(c @ 'B') | Some(c @ 'O') | Some(c @ 'D') | Some(c @ 'X') => {
			return Err(OryxError::new(
				ctx.pos_b,
				ctx.pos_a,
				format!(
					"Invalid number base specifier ‘{c}’, did you mean ‘{}’?",
					c.to_ascii_lowercase()
				),
			));
		},
		Some(c) if c.is_alphanumeric() => {
			return Err(OryxError::new(
				ctx.pos_b,
				ctx.pos_a,
				format!("Invalid number base specifier ‘{c}’"),
			));
		},
		_ => {
			return Err(OryxError::new(
				i,
				i + 1,
				"Expected number base specifier after ‘#’",
			));
		},
	};

	let (beg, end) = (ctx.pos_b, ctx.pos_a);
	let mut tok = match ctx.next() {
		Some(c) if alphabet.contains(c) => tokenize_number(ctx, alphabet)?,
		Some(c) if alphabet.len() == 16 && c.is_ascii_hexdigit() => {
			return Err(OryxError::new(
				ctx.pos_b,
				ctx.pos_a,
				format!("Hexadecimal digits must be uppercase"),
			));
		},
		Some(c) if c.is_alphanumeric() => {
			let base = base2str(alphabet.len());
			return Err(OryxError::new(
				ctx.pos_b,
				ctx.pos_a,
				format!("Invalid {base} digit ‘{c}’"),
			));
		},
		Some('\'') => {
			return Err(OryxError::new(
				ctx.pos_b,
				ctx.pos_a,
				format!(
					"Numeric literals may not begin with a digit separator"
				),
			));
		},
		_ => {
			let base = base2str(alphabet.len());
			return Err(OryxError::new(
				beg,
				end,
				format!("Expected {base} digit after base specifier"),
			));
		},
	};
	tok.view = Span(i, ctx.pos_a);
	return Ok(tok);
}

fn tokenize_number<'a>(
	ctx: &mut LexerContext<'a>,
	alphabet: &'static str,
) -> Result<Token, OryxError> {
	let i = ctx.pos_b;
	span_raw_number(ctx, alphabet, true)?;

	/* Fractional part */
	if ctx.peek().is_some_and(|c| c == '.') {
		ctx.next();
		if ctx.peek().is_some_and(|c| alphabet.contains(c)) {
			span_raw_number(ctx, alphabet, false)?;
		}
	}

	/* Exponential part */
	if ctx.peek().is_some_and(|c| c == 'e') {
		ctx.next();
		if ctx.peek().is_some_and(|c| c == '+' || c == '-') {
			ctx.next();
		}
		span_raw_number(ctx, alphabet, false)?;
	}

	return Ok(Token {
		kind: TokenType::Number,
		view: Span(i, ctx.pos_a),
	});
}

fn span_raw_number<'a>(
	ctx: &mut LexerContext<'a>,
	alphabet: &'static str,
	first_digit_lexed_p: bool,
) -> Result<(), OryxError> {
	if !first_digit_lexed_p {
		match ctx.next() {
			Some(c) if alphabet.contains(c) => c,
			Some(c) if alphabet.len() == 16 && c.is_ascii_hexdigit() => {
				return Err(OryxError::new(
					ctx.pos_b,
					ctx.pos_a,
					format!("Hexadecimal digits must be uppercase"),
				));
			},
			Some(c) if c.is_alphanumeric() => {
				let base = base2str(alphabet.len());
				return Err(OryxError::new(
					ctx.pos_b,
					ctx.pos_a,
					format!("Invalid {base} digit ‘{c}’"),
				));
			},
			_ => {
				let base = base2str(alphabet.len());
				return Err(OryxError::new(
					ctx.pos_b,
					ctx.pos_a,
					format!("Expected {base} digit"),
				));
			},
		};
	}

	let (mut beg, mut end) = (0, 0);
	let mut last_was_apos_p = false;
	while let Some(c) = ctx.peek() {
		match c {
			'\'' if last_was_apos_p => {
				return Err(OryxError::new(
					ctx.pos_b,
					ctx.pos_a + 1,
					"Numeric literals may not have adjecent digit separators",
				));
			},
			'\'' => {
				last_was_apos_p = true;
				ctx.next();
				(beg, end) = (ctx.pos_b, ctx.pos_a);
			},
			_ if alphabet.contains(c) => {
				last_was_apos_p = false;
				ctx.next();
			},
			_ => break,
		};
	}

	if last_was_apos_p {
		return Err(OryxError::new(
			beg,
			end,
			"Numeric literals may not end with a digit separator",
		));
	}

	return Ok(());
}

fn tokenize_string<'a>(ctx: &mut LexerContext<'a>) -> Result<Token, OryxError> {
	let i = ctx.pos_b;

	loop {
		match ctx.next() {
			Some(c) if c == '"' => break,
			Some(_) => {},
			None => {
				return Err(OryxError::new(
					i,
					ctx.pos_a,
					"Unterminated string literal",
				));
			},
		}
	}
	return Ok(Token {
		kind: TokenType::String,
		view: Span(i, ctx.pos_a),
	});
}

fn tokenize_identifier<'a>(ctx: &mut LexerContext<'a>) -> Token {
	let i = ctx.pos_b;
	while ctx.peek().is_some_and(unicode::xid_continue_p) {
		ctx.next();
	}
	let view = Span(i, ctx.pos_a);
	let kind = match KEYWORDS.get(&ctx.string[view.0..view.1]) {
		Some(kind) => kind.clone(),
		None => TokenType::Identifier,
	};
	return Token { kind, view };
}

fn base2str(n: usize) -> &'static str {
	return match n {
		2 => "binary",
		8 => "octal",
		10 => "decimal",
		16 => "hexadecimal",
		_ => unreachable!(),
	};
}
