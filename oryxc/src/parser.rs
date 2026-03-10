use soa_rs::{
	Soa,
	Soars,
};

use crate::errors::OryxError;
use crate::lexer::{
	Token,
	TokenType,
};
use crate::prelude::*;
use crate::size;

const MAX_PREC: i64 = 6;

/* Remember to edit the cases in Parser.node_leaf_*() when editing
 * this list! */
#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AstType {
	Assign,         /* (extra-data-lhs, extra-data-rhs) */
	BinaryOperator, /* (lhs, rhs) */
	Block,          /* (extra-data, extra-data-len) */
	Dereference,    /* (lhs, _) */
	Empty,          /* (_, _) */
	FunCall,        /* (expression, extra-data-exprs) */
	FunProto,       /* (extra-data-args, extra-data-rets) */
	Function,       /* (prototype, body) */
	Identifier,     /* (_, _) */
	MultiDefBind,   /* (extra-data-decls, extra-data-exprs) */
	Number,         /* (_, _) */
	Pointer,        /* (rhs, _) */
	Return,         /* (extra-data, extra-data-len) */
	Root,           /* (extra-data, extra-data-len) */
	String,         /* (_, _) */
	UnaryOperator,  /* (rhs, _) */
}

#[derive(Soars)]
#[soa_derive(Debug)]
pub struct AstNode {
	pub kind: AstType,
	pub tok:  u32,
	pub sub:  SubNodes,
}

#[derive(Debug)]
pub struct Ast {
	pub nodes: Soa<AstNode>,
	pub extra: Vec<u32>,
}

struct Parser<'a> {
	ast:        Soa<AstNode>,
	extra_data: Vec<u32>,
	cursor:     u32,
	scratch:    Vec<u32>,
	tokens:     &'a Soa<Token>,
	errors:     Vec<OryxError>,
}

impl<'a> Parser<'a> {
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
		return self.get_at(self.cursor);
	}

	#[inline(always)]
	fn get_at(&self, pos: u32) -> TokenType {
		return unsafe { *self.tokens.kind().get_unchecked(pos as usize) };
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
	fn new_error(&mut self, e: OryxError) {
		self.errors.push(e);
	}

	#[inline(always)]
	fn sync(&mut self, toks: &[TokenType]) {
		while !toks.contains(&self.next()) {}
	}

	fn scratch_guard<F, R>(&mut self, f: F) -> R
	where
		F: FnOnce(&mut Self) -> R,
	{
		let n = self.scratch.len();
		let res = f(self);
		self.scratch.truncate(n);
		return res;
	}

	fn node_span(&self, node: u32) -> (usize, usize) {
		let (lhs, rhs) = (self.node_leaf_l(node), self.node_leaf_r(node));
		unsafe {
			let lhs = self.ast.tok().get_unchecked(lhs as usize);
			let rhs = self.ast.tok().get_unchecked(rhs as usize);
			let lhs = self.tokens.view().get_unchecked(*lhs as usize).0;
			let rhs = self.tokens.view().get_unchecked(*rhs as usize).1;
			return (lhs, rhs);
		};
	}

	fn node_leaf_l(&self, node: u32) -> u32 {
		return match self.ast.kind()[node as usize] {
			AstType::Assign => {
				let i = self.ast.sub()[node as usize].0 + 1;
				let expr = self.extra_data[i as usize];
				self.node_leaf_l(expr)
			},
			AstType::BinaryOperator => {
				self.node_leaf_l(self.ast.sub()[node as usize].0)
			},
			AstType::Block => {
				let SubNodes(i, len) = self.ast.sub()[node as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_l(self.extra_data[i as usize])
				}
			},
			AstType::Dereference => {
				self.node_leaf_l(self.ast.sub()[node as usize].0)
			},
			AstType::Empty => node,
			AstType::FunCall => {
				self.node_leaf_l(self.ast.sub()[node as usize].0)
			},
			AstType::FunProto => node,
			AstType::Function => node,
			AstType::Identifier => node,
			AstType::MultiDefBind => node,
			AstType::Number => node,
			AstType::Pointer => node,
			AstType::Return => node,
			AstType::Root => {
				let SubNodes(i, len) = self.ast.sub()[node as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_l(self.extra_data[i as usize])
				}
			},
			AstType::String => node,
			AstType::UnaryOperator => node,
		};
	}

	fn node_leaf_r(&self, node: u32) -> u32 {
		return match self.ast.kind()[node as usize] {
			AstType::Assign => {
				let i = self.ast.sub()[node as usize].1;
				let nexprs = self.extra_data[i as usize];
				self.node_leaf_r(self.extra_data[(i + nexprs) as usize])
			},
			AstType::BinaryOperator => {
				self.node_leaf_r(self.ast.sub()[node as usize].1)
			},
			AstType::Block => {
				let SubNodes(i, len) = self.ast.sub()[node as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_r(self.extra_data[(i + len - 1) as usize])
				}
			},
			AstType::Dereference => node,
			AstType::Empty => node,
			AstType::FunCall => {
				let i = self.ast.sub()[node as usize].1;
				let len = self.extra_data[i as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_r(self.extra_data[(i + len) as usize])
				}
			},
			AstType::FunProto => {
				let SubNodes(i, j) = self.ast.sub()[node as usize];
				let jlen = self.extra_data[j as usize];
				if jlen == 0 {
					let ilen = self.extra_data[i as usize];
					if ilen == 0 {
						node
					} else {
						self.node_leaf_r(self.extra_data[(i + ilen) as usize])
					}
				} else {
					self.node_leaf_r(self.extra_data[(j + jlen) as usize])
				}
			},
			AstType::Function => {
				self.node_leaf_r(self.ast.sub()[node as usize].1)
			},
			AstType::Identifier => node,
			AstType::MultiDefBind => {
				let i = self.ast.sub()[node as usize].1;
				let len = self.extra_data[i as usize];
				self.node_leaf_r(self.extra_data[(i + len) as usize])
			},
			AstType::Number => node,
			AstType::Pointer => {
				self.node_leaf_r(self.ast.sub()[node as usize].0)
			},
			AstType::Return => {
				let SubNodes(i, len) = self.ast.sub()[node as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_r(self.extra_data[(i + len - 1) as usize])
				}
			},
			AstType::Root => {
				let SubNodes(i, len) = self.ast.sub()[node as usize];
				if len == 0 {
					node
				} else {
					self.node_leaf_r(self.extra_data[(i + len - 1) as usize])
				}
			},
			AstType::String => node,
			AstType::UnaryOperator => {
				self.node_leaf_r(self.ast.sub()[node as usize].0)
			},
		};
	}

	fn parse_toplevel(&mut self) {
		let mut syncp = false;
		let k = match self.get() {
			TokenType::KeywordDef => match self.parse_def() {
				Ok(e) => e,
				Err(e) => {
					self.new_error(e);
					syncp = true;
					u32::MAX
				},
			},
			TokenType::Eof => return,
			_ => {
				self.new_error(OryxError::new(
					self.get_view(),
					format!(
						"expected top-level statement but got {:?}", /* TODO: Impl Display for TokenType */
						self.get(),
					),
				));
				syncp = true;
				u32::MAX
			},
		};

		if syncp {
			self.sync(&[TokenType::Eof, TokenType::KeywordDef]);
		} else {
			self.scratch.push(k);
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
			TokenType::KeywordReturn => {
				let main_tok = self.cursor;
				self.next(); /* Consume ‘return’ */
				self.scratch_guard(|p| {
					let exprbeg = match p.parse_expr_list() {
						Ok(i) => i,
						Err(e) => {
							p.new_error(e);
							syncp = true;
							return u32::MAX;
						},
					};
					if p.get_n_move() != TokenType::Semicolon {
						p.new_error(OryxError::new(
							(
								p.get_view_at(main_tok).0,
								p.get_view_at(p.cursor - 1).1,
							),
							"expected semicolon after return statement",
						));
						syncp = true;
						return u32::MAX;
					}

					let nexprs = p.scratch.len() - exprbeg;
					let extra_data_beg = p.extra_data.len();
					for x in &p.scratch[exprbeg..] {
						p.extra_data.push(*x);
					}
					return p.new_node(AstNode {
						kind: AstType::Return,
						tok:  main_tok,
						sub:  SubNodes(extra_data_beg as u32, nexprs as u32),
					});
				})
			},
			t if t.exprp() => {
				/* Här kan vi antigen ha ett uttryck (t.ex. ‘foo()’)
				 * eller en uttyrckslista som används i en tilldelning
				 * (t.ex. ‘x, y = 69, 420’) */

				match self.scratch_guard(|p| {
					let lhs = p.parse_expr_list()?;
					let nlexprs = p.scratch.len() - lhs;

					if nlexprs == 1 && p.get() != TokenType::Equals {
						if p.get_at(p.cursor - 1) == TokenType::Comma {
							return Err(OryxError::new(
								p.get_view_at(p.cursor - 1),
								"unexpected comma after expression",
							));
						}
						if p.get_n_move() != TokenType::Semicolon {
							let k = p.scratch[lhs];
							return Err(OryxError::new(
								p.node_span(k),
								"expected semicolon after expression",
							));
						}
						return Ok(p.scratch[lhs]);
					}

					if p.get_at(p.cursor - 1) == TokenType::Comma {
						/* Returnera inte felet eftersom återställningen är
						 * implicit */
						p.new_error(OryxError::new(
							p.get_view_at(p.cursor - 1),
							"assignment expression lists do not accept trailing commas",
						));
					}

					let main_tok = p.cursor;
					if p.get_n_move() != TokenType::Equals {
						let lexpr = p.scratch[lhs];
						let rexpr = p.scratch[lhs + nlexprs - 1];
						return Err(OryxError::new(
							(p.node_span(lexpr).0, p.node_span(rexpr).1),
							"expected ‘=’ operator after expression list",
						));
					}

					let rhs = p.parse_expr_list()?;
					let nrexprs = p.scratch.len() - rhs;

					if nrexprs == 0 {
						return Err(OryxError::new(
							p.get_view_at(main_tok),
							"expected expression(s) on the right-hand side of assignment",
						));
					}
					if p.get_at(p.cursor - 1) == TokenType::Comma {
						/* Returnera inte felet eftersom återställningen är
						 * implicit */
						p.new_error(OryxError::new(
							p.get_view_at(p.cursor - 1),
							"assignment expression lists do not accept trailing commas",
						));
					}

					let lhsbeg = p.extra_data.len();
					p.extra_data.push(nlexprs as u32);
					for x in &p.scratch[lhs..rhs] {
						p.extra_data.push(*x);
					}
					let rhsbeg = p.extra_data.len();
					p.extra_data.push(nrexprs as u32);
					for x in &p.scratch[rhs..] {
						p.extra_data.push(*x);
					}

					return Ok(p.new_node(AstNode {
						kind: AstType::Assign,
						tok:  main_tok,
						sub:  SubNodes(lhsbeg as u32, rhsbeg as u32),
					}));
				}) {
					Ok(e) => e,
					Err(e) => {
						self.new_error(e);
						syncp = true;
						u32::MAX
					},
				}
			},
			_ => {
				self.new_error(OryxError::new(
					self.get_view(),
					format!("expected statement but got {:?}", self.get()),
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
				"expected ‘def’",
			));
		}
		return self.scratch_guard(|p| {
			let lhs = p.parse_decl_list()?;
			if p.scratch.len() - lhs == 0 {
				return Err(OryxError::new(
					p.get_view_at(main_tok),
					"expected an identifier",
				));
			}

			let t = p.get_n_move();
			if t != TokenType::Equals {
				return Err(if t == TokenType::Semicolon {
					OryxError::new(
						(p.get_view_at(main_tok).0, p.get_view().1),
						"symbols defined with ‘def’ must be initialized",
					)
				} else {
					OryxError::new(p.get_view_at(p.cursor - 1), "expected ‘=’")
				});
			}

			let rhs = p.parse_expr_list()?;
			if p.scratch.len() - rhs == 0 {
				return Err(OryxError::new(
					p.get_view_at(p.cursor - 1),
					"expected expression after ‘=’",
				));
			}
			if p.get_n_move() != TokenType::Semicolon {
				return Err(OryxError::new(
					p.get_view_at(p.cursor - 1),
					"expected semicolon",
				));
			}

			let ndecls = (rhs - lhs) / 2;
			let nexprs = p.scratch.len() - rhs;
			let declbeg = p.extra_data.len();
			let exprbeg = declbeg + ndecls * 2 + 1;

			p.extra_data.push(ndecls as u32);
			for x in &p.scratch[lhs..rhs] {
				p.extra_data.push(*x);
			}
			p.extra_data.push(nexprs as u32);
			for x in &p.scratch[rhs..] {
				p.extra_data.push(*x);
			}

			return Ok(p.new_node(AstNode {
				kind: AstType::MultiDefBind,
				tok:  main_tok,
				sub:  SubNodes(declbeg as u32, exprbeg as u32),
			}));
		});
	}

	fn parse_func_proto(&mut self) -> Result<u32, OryxError> {
		let main_tok = self.cursor;

		if self.next() != TokenType::ParenL {
			return Err(OryxError::new(
				self.get_view_at(main_tok),
				"expected an argument list after the ‘func’ keyword",
			));
		}

		let parenl = self.cursor;
		self.next(); /* Consume ‘(’ */
		return self.scratch_guard(|p| {
			let lhs = p.parse_decl_list()?;

			if p.get_n_move() != TokenType::ParenR {
				/* TODO: Highlight the entire argument list */
				return Err(OryxError::new(
					p.get_view_at(parenl),
					"parameter list missing a closing parenthesis",
				));
			}

			let t = p.get();
			let rhs = match t {
				TokenType::ParenL => {
					let parenl = p.cursor;
					p.next(); /* Consume ‘(’ */
					let i = p.parse_expr_list()?;
					if p.get_n_move() != TokenType::ParenR {
						/* TODO: Highlight the entire return list */
						return Err(OryxError::new(
							p.get_view_at(parenl),
							"return list missing closing parenthesis",
						));
					}
					i
				},
				_ if t.exprp() => {
					let k = p.parse_expr(0)?;
					p.scratch.push(k);
					p.scratch.len() - 1
				},
				_ => p.scratch.len(),
			};

			let nargs = (rhs - lhs) / 2;
			let nrets = p.scratch.len() - rhs;
			let argbeg = p.extra_data.len();
			let retbeg = argbeg + nargs * 2 + 1;

			p.extra_data.push(nargs as u32);
			for x in &p.scratch[lhs..rhs] {
				p.extra_data.push(*x);
			}
			p.extra_data.push(nrets as u32);
			for x in &p.scratch[rhs..] {
				p.extra_data.push(*x);
			}

			return Ok(p.new_node(AstNode {
				kind: AstType::FunProto,
				tok:  main_tok,
				sub:  SubNodes(argbeg as u32, retbeg as u32),
			}));
		});
	}

	fn parse_block(&mut self) -> Result<u32, OryxError> {
		let main_tok = self.cursor;
		if self.get_n_move() != TokenType::BraceL {
			return Err(OryxError::new(
				self.get_view_at(self.cursor - 1),
				"expected opening brace",
			));
		}

		return self.scratch_guard(|p| {
			let scratch_beg = p.scratch.len();
			while p.get() != TokenType::BraceR {
				let k = p.parse_stmt();
				p.scratch.push(k);
			}
			p.next(); /* Consume ‘}’ */

			let extra_data_beg = p.extra_data.len();
			let nstmts = (p.scratch.len() - scratch_beg) as u32;

			for x in &p.scratch[scratch_beg..] {
				p.extra_data.push(*x);
			}
			return Ok(p.new_node(AstNode {
				kind: AstType::Block,
				tok:  main_tok,
				sub:  SubNodes(extra_data_beg as u32, nstmts),
			}));
		});
	}

	fn parse_decl_list(&mut self) -> Result<usize, OryxError> {
		let scratch_beg = self.scratch.len();
		let mut nuntyped = 0;
		loop {
			if self.get() != TokenType::Identifier {
				break;
			}
			self.scratch.push(self.cursor);
			self.scratch.push(u32::MAX);
			nuntyped += 1;

			match self.next() {
				TokenType::Comma => {
					self.next();
				},
				t if t.exprp() => {
					let k = self.parse_expr(0)?;
					let len = self.scratch.len();
					for i in 0..nuntyped {
						self.scratch[len - 1 - 2 * i] = k;
					}
					nuntyped = 0;
				},
				_ => break,
			};
		}

		return Ok(scratch_beg);
	}

	fn parse_expr_list(&mut self) -> Result<usize, OryxError> {
		let scratch_beg = self.scratch.len();

		while self.get().exprp() {
			let k = match self.parse_expr(0) {
				Ok(e) => e,
				Err(e) => {
					self.scratch.truncate(scratch_beg);
					return Err(e);
				},
			};
			self.scratch.push(k);
			if self.get() == TokenType::Comma {
				self.next();
			} else {
				break;
			}
		}

		return Ok(scratch_beg);
	}

	fn parse_expr(&mut self, minprec: i64) -> Result<u32, OryxError> {
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
				| TokenType::Percent
				| TokenType::Percent2
				| TokenType::SlashPercent
				| TokenType::Slash => 5,
				TokenType::Bar
				| TokenType::Minus
				| TokenType::Plus
				| TokenType::Tilde => 4,
				TokenType::AngleL
				| TokenType::AngleLEquals
				| TokenType::AngleR
				| TokenType::AngleREquals
				| TokenType::BangEquals
				| TokenType::Equals2 => 3,
				TokenType::Ampersand2 => 2,
				TokenType::Bar2 => 1,
				_ => 0,
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
			| TokenType::Bang
			| TokenType::Minus
			| TokenType::Plus
			| TokenType::Tilde => {
				let i = self.cursor;
				self.next();
				let rhs = self.parse_expr(MAX_PREC)?;
				self.new_node(AstNode {
					kind: AstType::UnaryOperator,
					tok:  i,
					sub:  SubNodes(rhs, u32::MAX),
				})
			},
			TokenType::ParenL => {
				let parenl = self.cursor;
				self.next();
				let k = self.parse_expr(0)?;
				if self.get() != TokenType::ParenR {
					return Err(OryxError::new(
						self.get_view_at(parenl),
						"expression missing closing parenthesis",
					));
				}
				self.next(); /* Consume ‘)’ */
				k
			},
			TokenType::Caret => {
				let tok = self.cursor;
				self.next();
				let k = self.parse_expr(MAX_PREC)?;
				self.new_node(AstNode {
					kind: AstType::Pointer,
					tok,
					sub: SubNodes(k, u32::MAX),
				})
			},
			TokenType::KeywordFunc => {
				let tok = self.cursor;
				let proto = self.parse_func_proto()?;
				if self.get() == TokenType::BraceL {
					let body = self.parse_block()?;
					self.new_node(AstNode {
						kind: AstType::Function,
						tok,
						sub: SubNodes(proto, body),
					})
				} else {
					proto
				}
			},
			_ => {
				return Err(OryxError::new(
					self.get_view(),
					"expected expression",
				));
			},
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
				| TokenType::Ampersand2
				| TokenType::AmpersandTilde
				| TokenType::AngleL
				| TokenType::AngleL2
				| TokenType::AngleL3
				| TokenType::AngleLEquals
				| TokenType::AngleR
				| TokenType::AngleR2
				| TokenType::AngleR3
				| TokenType::AngleREquals
				| TokenType::Asterisk
				| TokenType::BangEquals
				| TokenType::Bar
				| TokenType::Bar2
				| TokenType::Equals2
				| TokenType::Minus
				| TokenType::Percent
				| TokenType::Percent2
				| TokenType::Plus
				| TokenType::Slash
				| TokenType::SlashPercent
				| TokenType::Tilde => {
					let i = self.cursor;
					self.next();
					let rhs = self.parse_expr(prec)?;
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
					self.scratch_guard(|p| {
						let exprbeg = p.parse_expr_list()?;
						match p.get_n_move() {
							TokenType::ParenR => {},
							TokenType::Comma => {
								return Err(OryxError::new(
									p.get_view_at(p.cursor - 1),
									"empty function parameter",
								));
							},
							_ => {
								return Err(OryxError::new(
									/* TODO: Highlight the entire argument
									 * list */
									p.get_view_at(tok),
									"function call missing closing parenthesis",
								));
							},
						};
						let nexprs = p.scratch.len() - exprbeg;
						let extra_data_beg = p.extra_data.len();
						p.extra_data.push(nexprs as u32);
						for x in &p.scratch[exprbeg..] {
							p.extra_data.push(*x);
						}
						return Ok(p.new_node(AstNode {
							kind: AstType::FunCall,
							tok,
							sub: SubNodes(lhs, extra_data_beg as u32),
						}));
					})?
				},

				_ => break,
			}
		}

		return Ok(lhs);
	}
}

pub fn parse(tokens: &Soa<Token>) -> Result<Ast, Vec<OryxError>> {
	let mut p = Parser::new(tokens);
	while p.get() != TokenType::Eof {
		p.parse_toplevel();
	}
	if p.errors.len() != 0 {
		return Err(p.errors);
	}

	let stmtsbeg = p.extra_data.len();
	let nstmts = p.scratch.len();
	p.extra_data.append(&mut p.scratch);
	p.new_node(AstNode {
		kind: AstType::Root,
		tok:  0,
		sub:  SubNodes(stmtsbeg as u32, nstmts as u32),
	});
	return Ok(Ast {
		nodes: p.ast,
		extra: p.extra_data,
	});
}
