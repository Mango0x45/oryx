use std::mem::ManuallyDrop;
use std::process;
use std::vec::Vec;

use soa_rs::{
	Soa,
	Soars,
};

use crate::errors::OryxError;
use crate::lexer::{
	Token,
	TokenType,
};
use crate::size;

const MIN_PREC: i64 = 0;
const MAX_PREC: i64 = 6;

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AstType {
	Assign,         /* (ident-token, expression) */
	Block,          /* (extra-data, _) */
	Dereference,    /* (lhs, _) */
	Empty,          /* (_, _) */
	FunCall,        /* (expression, extra-data) */
	FunProto,       /* (extra-data, _) */
	Function,       /* (prototype, body) */
	Identifier,     /* (_, _) */
	MultiDefBind,   /* (extra-data, _) */
	Number,         /* (_, _) */
	Pointer,        /* (rhs, _) */
	Return,         /* (extra-data, _) */
	String,         /* (_, _) */
	UnaryOperator,  /* (rhs, _) */
	BinaryOperator, /* (lhs, rhs) */
}

#[derive(Clone, Copy, Debug)]
pub struct SubNodes(u32, u32);

impl Default for SubNodes {
	fn default() -> Self {
		return Self(u32::MAX, u32::MAX);
	}
}

#[derive(Soars)]
#[soa_derive(Debug)]
pub struct AstNode {
	pub kind: AstType,
	pub tok:  u32,
	pub sub:  SubNodes,
}

pub struct DeclData {
	lhs: Vec<(u32, u32)>, /* (ident, type) tuple */
	rhs: Vec<u32>,
}

pub struct FunCallData {
	args: Vec<u32>,
}

pub struct FunProtoData {
	args: Vec<(u32, u32)>, /* (ident, type) tuple */
	ret:  Vec<u32>,
}

pub struct BlockData {
	stmts: Vec<u32>,
}

pub struct ReturnData {
	exprs: Vec<u32>,
}

pub union ExtraData {
	block:    ManuallyDrop<BlockData>,
	decl:     ManuallyDrop<DeclData>,
	funcall:  ManuallyDrop<FunCallData>,
	funproto: ManuallyDrop<FunProtoData>,
	r#return: ManuallyDrop<ReturnData>,
}

struct Parser<'a> {
	ast:        Soa<AstNode>,
	extra_data: Vec<ExtraData>,
	cursor:     u32,
	scratch:    Vec<u32>,
	tokens:     &'a Soa<Token>,
	errors:     Vec<OryxError>,
}

impl<'a> Parser<'a> {
	fn err_at_position(&self, i: u32, msg: &str) -> ! {
		for e in &self.errors {
			eprintln!("{e}");
		}
		process::exit(69);
	}

	fn new(tokens: &'a Soa<Token>) -> Self {
		return Self {
			ast: Soa::with_capacity(size::kibibytes(10)),
			extra_data: Vec::with_capacity(size::kibibytes(1)),
			cursor: 0,
			scratch: Vec::with_capacity(64),
			tokens,
			errors: Vec::new(),
		};
	}

	#[inline(always)]
	fn get(&self) -> TokenType {
		return unsafe {
			*self.tokens.kind().get_unchecked(self.cursor as usize)
		};
	}

	#[inline(always)]
	fn get_view(&self) -> (usize, usize) {
		return self.get_view_at(self.cursor);
	}

	#[inline(always)]
	fn get_view_at(&self, pos: u32) -> (usize, usize) {
		return unsafe { *self.tokens.view().get_unchecked(pos as usize) };
	}

	#[inline(always)]
	fn next(&mut self) -> TokenType {
		self.cursor += 1;
		return self.get();
	}

	#[inline(always)]
	fn get_n_move(&mut self) -> TokenType {
		let t = self.get();
		self.cursor += 1;
		return t;
	}

	#[inline(always)]
	fn new_node(&mut self, n: AstNode) -> u32 {
		self.ast.push(n);
		return (self.ast.len() - 1) as u32;
	}

	#[inline(always)]
	fn new_extra_data(&mut self, d: ExtraData) -> u32 {
		self.extra_data.push(d);
		return (self.extra_data.len() - 1) as u32;
	}

	#[inline(always)]
	fn new_error(&mut self, e: OryxError) {
		self.errors.push(e);
	}

	#[inline(always)]
	fn sync(&mut self, toks: &[TokenType]) {
		while !toks.contains(&self.next()) {}
	}

	fn node_span(&self, node: u32) -> (usize, usize) {
		let toks = self.ast.tok();
		let views = self.tokens.view();
		let (lhs, rhs) = self.node_span_1(node);
		let (lhs, rhs) = (toks[lhs as usize], toks[rhs as usize]);
		return (views[lhs as usize].0, views[rhs as usize].1);
	}

	fn node_span_1(&self, node: u32) -> (u32, u32) {
		let SubNodes(_0, _1) = self.ast.sub()[node as usize];
		return match self.ast.kind()[node as usize] {
			AstType::Assign => (self.node_span_1(_0).0, self.node_span_1(_1).1),
			AstType::Block => {
				todo!()
			},
			/* (extra-data, _) */
			AstType::Dereference => (self.node_span_1(_0).0, node),
			AstType::Empty => (node, node),
			AstType::FunCall => {
				todo!()
			},
			/* (expression, extra-data) */
			AstType::FunProto => {
				todo!()
			},
			/* (extra-data, _) */
			AstType::Function => {
				todo!()
			},
			/* (prototype, body) */
			AstType::Identifier => (node, node),
			AstType::MultiDefBind => {
				todo!()
			},
			/* (extra-data, _) */
			AstType::Number => (node, node),
			AstType::Pointer => (node, self.node_span_1(_0).1),
			AstType::Return => {
				let exprs =
					unsafe { &self.extra_data[_0 as usize].r#return.exprs };
				if exprs.len() == 0 {
					(node, node)
				} else {
					let last = *exprs.last().unwrap();
					(node, self.node_span_1(last).1)
				}
			},
			AstType::String => (node, node),
			AstType::UnaryOperator => (node, self.node_span_1(_0).1),
			AstType::BinaryOperator => {
				(self.node_span_1(_0).0, self.node_span_1(_1).1)
			},
		};
	}

	fn parse_toplevel(&mut self) {
		let mut syncp = false;
		match self.get() {
			TokenType::KeywordDef => match self.parse_def() {
				Ok(_) => {},
				Err(e) => {
					self.new_error(e);
					syncp = true;
				},
			},
			TokenType::Eof => return,
			_ => {
				self.new_error(OryxError::new(
					self.get_view(),
					format!(
						"Expected top-level statement but got {:?}",
						self.get(),
					),
				));
				syncp = true;
			},
		};

		if syncp {
			self.sync(&[TokenType::Eof, TokenType::KeywordDef]);
		}
	}

	fn parse_stmt(&mut self) -> u32 {
		let mut syncp = false;
		let ret = match self.get() {
			TokenType::Semicolon => {
				self.next(); /* Consume ‘}’ */
				self.new_node(AstNode {
					kind: AstType::Empty,
					tok:  self.cursor - 1,
					sub:  SubNodes::default(),
				})
			},
			TokenType::KeywordDef => match self.parse_def() {
				Ok(n) => n,
				Err(e) => {
					self.new_error(e);
					syncp = true;
					u32::MAX /* Dummy value */
				},
			},
			TokenType::KeywordReturn => 'label: {
				let main_tok = self.cursor;
				self.next(); /* Consume ‘return’ */
				let exprs = self.parse_expr_list();
				if self.get_n_move() != TokenType::Semicolon {
					self.new_error(OryxError::new(
						(
							self.get_view_at(main_tok).0,
							self.get_view_at(self.cursor - 1).1,
						),
						"Expected semicolon after return statement",
					));
					syncp = true;
					break 'label u32::MAX;
				}
				let i = self.new_extra_data(ExtraData {
					r#return: ManuallyDrop::new(ReturnData { exprs }),
				});
				self.new_node(AstNode {
					kind: AstType::Return,
					tok:  main_tok,
					sub:  SubNodes(i, u32::MAX),
				})
			},
			t if t.exprp() => {
				let k = self.parse_expr(MIN_PREC);
				if self.get_n_move() != TokenType::Semicolon {
					self.new_error(OryxError::new(
						self.node_span(k),
						"Expected semicolon after expression",
					));
					syncp = true;
				}
				k
			},
			_ => {
				self.new_error(OryxError::new(
					self.get_view(),
					format!("Expected statement but got {:?}", self.get()),
				));
				syncp = true;
				u32::MAX
			},
		};

		if syncp {
			self.sync(&[
				TokenType::BraceR,
				TokenType::Eof,
				TokenType::KeywordDef,
				TokenType::KeywordReturn,
				TokenType::Semicolon,
			]);
		}

		return ret;
	}

	fn parse_def(&mut self) -> Result<u32, OryxError> {
		let main_tok = self.cursor;
		if self.get_n_move() != TokenType::KeywordDef {
			return Err(OryxError::new(
				self.get_view_at(self.cursor - 1),
				"Expected ‘def’",
			));
		}
		let lhs = self.parse_decl_list();
		if lhs.len() == 0 {
			return Err(OryxError::new(
				self.get_view_at(main_tok),
				"Expected an identifier",
			));
		}

		let t = self.get_n_move();
		if t != TokenType::Equals {
			return Err(if t == TokenType::Semicolon {
				OryxError::new(
					(self.get_view_at(main_tok).0, self.get_view().1),
					"Symbols defined with ‘def’ must be initialized",
				)
			} else {
				OryxError::new(
					self.get_view_at(self.cursor - 1),
					"Expected ‘=’",
				)
			});
		}

		let rhs = self.parse_expr_list();
		if rhs.len() == 0 {
			return Err(OryxError::new(
				self.get_view_at(self.cursor - 1),
				"Expected expression after ‘=’",
			));
		}
		if self.get_n_move() != TokenType::Semicolon {
			return Err(OryxError::new(
				self.get_view_at(self.cursor - 1),
				"Expected semicolon",
			));
		}

		let i = self.new_extra_data(ExtraData {
			decl: ManuallyDrop::new(DeclData { lhs, rhs }),
		});
		return Ok(self.new_node(AstNode {
			kind: AstType::MultiDefBind,
			tok:  main_tok,
			sub:  SubNodes(i as u32, u32::MAX),
		}));
	}

	fn parse_func_proto(&mut self) -> u32 {
		let main_tok = self.cursor;

		/* No params or return */
		if self.next() != TokenType::ParenL {
			return self.new_node(AstNode {
				kind: AstType::FunProto,
				tok:  main_tok,
				sub:  SubNodes::default(),
			});
		}

		self.next(); /* Consume ‘(’ */
		let args = self.parse_decl_list();

		if self.get_n_move() != TokenType::ParenR {
			self.err_at_position(
				self.cursor - 1,
				"Expected closing parenthesis",
			);
		}

		let t = self.get();
		let ret = match t {
			TokenType::ParenL => {
				self.next(); /* Consume ‘(’ */
				let xs = self.parse_expr_list();
				if self.get_n_move() != TokenType::ParenR {
					self.err_at_position(
						self.cursor - 1,
						"Expected closing parenthesis",
					);
				}
				xs
			},
			_ if t.exprp() => {
				// TODO: This is really bad. We should probably optimize
				// for the small cases (or use an arena?)
				vec![self.parse_expr(MIN_PREC)]
			},
			_ => Vec::new(), /* Doesn’t allocate */
		};

		let i = self.new_extra_data(ExtraData {
			funproto: ManuallyDrop::new(FunProtoData { args, ret }),
		});
		return self.new_node(AstNode {
			kind: AstType::FunProto,
			tok:  main_tok,
			sub:  SubNodes(i, u32::MAX),
		});
	}

	fn parse_block(&mut self) -> u32 {
		let main_tok = self.cursor;
		if self.get_n_move() != TokenType::BraceL {
			self.err_at_position(self.cursor - 1, "Expected opening brace");
		}

		let mut stmts = Vec::<u32>::with_capacity(64);
		while self.get() != TokenType::BraceR {
			stmts.push(self.parse_stmt());
		}
		self.next(); /* Consume ‘}’ */
		let i = self.new_extra_data(ExtraData {
			block: ManuallyDrop::new(BlockData { stmts }),
		});
		return self.new_node(AstNode {
			kind: AstType::Block,
			tok:  main_tok,
			sub:  SubNodes(i, u32::MAX),
		});
	}

	fn parse_decl_list(&mut self) -> Vec<(u32, u32)> {
		let scratch_beg = self.scratch.len();
		let (mut nidents, mut nuntyped) = (0, 0);
		loop {
			if self.get() != TokenType::Identifier {
				break;
			}
			self.scratch.push(self.cursor);
			self.scratch.push(u32::MAX);
			nidents += 1;
			nuntyped += 1;

			match self.next() {
				TokenType::Comma => {
					self.next();
				},
				t if t.exprp() => {
					let k = self.parse_expr(MIN_PREC);
					let len = self.scratch.len();
					for i in 0..nuntyped {
						self.scratch[len - 1 - 2 * i] = k;
					}
					nuntyped = 0;
				},
				_ => break,
			};
		}

		let mut iter = self.scratch.drain(scratch_beg..);
		let mut pairs = Vec::with_capacity(nidents);
		while let (Some(a), Some(b)) = (iter.next(), iter.next()) {
			pairs.push((a, b));
		}
		return pairs;
	}

	fn parse_expr_list(&mut self) -> Vec<u32> {
		let scratch_beg = self.scratch.len();

		while self.get().exprp() {
			let k = self.parse_expr(MIN_PREC);
			self.scratch.push(k);
			if self.get() == TokenType::Comma {
				self.next();
			} else {
				break;
			}
		}

		return self.scratch.drain(scratch_beg..).collect();
	}

	fn parse_expr(&mut self, minprec: i64) -> u32 {
		fn getprec(t: TokenType) -> i64 {
			match t {
				TokenType::ParenL => 6,
				TokenType::Ampersand
				| TokenType::AmpersandTilde
				| TokenType::AngleL2
				| TokenType::AngleL3
				| TokenType::AngleR2
				| TokenType::AngleR3
				| TokenType::Asterisk
				| TokenType::Slash => 5,
				TokenType::Bar
				| TokenType::Minus
				| TokenType::Plus
				| TokenType::Tilde => 4,
				TokenType::AngleL | TokenType::AngleR => 3,
				_ => -1,
			}
		}

		let mut lhs = match self.get() {
			TokenType::Identifier => {
				self.next();
				self.new_node(AstNode {
					kind: AstType::Identifier,
					tok:  self.cursor - 1,
					sub:  SubNodes::default(),
				})
			},
			TokenType::Number => {
				self.next();
				self.new_node(AstNode {
					kind: AstType::Number,
					tok:  self.cursor - 1,
					sub:  SubNodes::default(),
				})
			},
			TokenType::String => {
				self.next();
				self.new_node(AstNode {
					kind: AstType::String,
					tok:  self.cursor - 1,
					sub:  SubNodes::default(),
				})
			},
			TokenType::Ampersand
			| TokenType::Exclamation
			| TokenType::Minus
			| TokenType::Plus
			| TokenType::Tilde => {
				let i = self.cursor;
				self.next();
				let rhs = self.parse_expr(MAX_PREC);
				self.new_node(AstNode {
					kind: AstType::UnaryOperator,
					tok:  i,
					sub:  SubNodes(rhs, u32::MAX),
				})
			},
			TokenType::ParenL => {
				self.next();
				let k = self.parse_expr(MIN_PREC);
				if self.get() != TokenType::ParenR {
					self.err_at_position(
						self.cursor,
						"Expected closing parenthesis",
					);
				}
				self.next(); /* Consume ‘)’ */
				k
			},
			TokenType::Caret => {
				let tok = self.cursor;
				self.next();
				let k = self.parse_expr(MAX_PREC);
				self.new_node(AstNode {
					kind: AstType::Pointer,
					tok,
					sub: SubNodes(k, u32::MAX),
				})
			},
			TokenType::KeywordFunc => {
				let tok = self.cursor;
				let proto = self.parse_func_proto();
				if self.get() == TokenType::BraceL {
					let body = self.parse_block();
					self.new_node(AstNode {
						kind: AstType::Function,
						tok,
						sub: SubNodes(proto, body),
					})
				} else {
					proto
				}
			},
			_ => self.err_at_position(self.cursor, "Expected expression"),
		};

		loop {
			let tok = self.get();
			let prec = getprec(tok);
			if prec < minprec {
				break;
			}

			lhs = match tok {
				/* Binop */
				TokenType::Ampersand
				| TokenType::AmpersandTilde
				| TokenType::AngleL2
				| TokenType::AngleL3
				| TokenType::AngleR2
				| TokenType::AngleR3
				| TokenType::Asterisk
				| TokenType::Slash
				| TokenType::Bar
				| TokenType::Minus
				| TokenType::Plus
				| TokenType::Tilde
				| TokenType::AngleL
				| TokenType::AngleR => {
					let i = self.cursor;
					self.next();
					let rhs = self.parse_expr(prec);
					self.new_node(AstNode {
						kind: AstType::BinaryOperator,
						tok:  i,
						sub:  SubNodes(lhs, rhs),
					})
				},

				/* Dereference */
				TokenType::Caret => {
					self.next();
					self.new_node(AstNode {
						kind: AstType::Dereference,
						tok:  self.cursor - 1,
						sub:  SubNodes(lhs, u32::MAX),
					})
				},

				/* Funcall */
				TokenType::ParenL => {
					let tok = self.cursor;
					self.next();
					let args = self.parse_expr_list();
					if self.get_n_move() != TokenType::ParenR {
						self.err_at_position(self.cursor - 1, "Expected ‘)’");
					}
					let i = self.new_extra_data(ExtraData {
						funcall: ManuallyDrop::new(FunCallData { args }),
					});
					self.new_node(AstNode {
						kind: AstType::FunCall,
						tok,
						sub: SubNodes(lhs, i),
					})
				},

				_ => break,
			}
		}

		return lhs;
	}
}

pub fn parse(
	tokens: &Soa<Token>,
) -> Result<(Soa<AstNode>, Vec<ExtraData>), Vec<OryxError>> {
	let mut p = Parser::new(tokens);
	while p.get() != TokenType::Eof {
		p.parse_toplevel();
	}
	return if p.errors.len() != 0 {
		Err(p.errors)
	} else {
		Ok((p.ast, p.extra_data))
	};
}
