use std::fmt::{
	self,
	Debug,
	Formatter,
};

use crate::hashtrie::HTrie;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FileId(pub usize);

#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SymbolId(pub u32);

#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(pub usize);

impl ScopeId {
	pub const GLOBAL: Self = Self(0);
	pub const INVALID: Self = Self(usize::MAX);
}

#[derive(Debug)]
pub struct Scope {
	pub parent: ScopeId,
	pub symtab: HTrie<SymbolId, Symbol>,
}

impl Scope {
	pub fn new(parent: ScopeId) -> Self {
		return Self {
			parent,
			symtab: HTrie::new(),
		};
	}
}

#[repr(u8)]
#[derive(Debug, Default)]
pub enum ResolutionState {
	#[default]
	Unresolved,
	Resolving,
	Resolved,
	Poisoned,
}

#[derive(Debug, Default)]
pub struct Symbol {
	pub state: ResolutionState,
	pub kind:  u32,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DeclKind {
	ConstDef = 0,
	Param    = 1,
}

pub enum OryxType {
	Integer { bits: usize, signed: bool },
	Boolean,
	Pointer { base: u32 },
	Function { args: Vec<u32>, rets: Vec<u32> },
}

#[derive(Clone, Copy)]
pub struct SubNodes(pub u32, pub u32);

impl Debug for SubNodes {
	fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
		let __ = format_args!("_");
		return f
			.debug_tuple("SubNodes")
			.field(if self.0 != u32::MAX { &self.0 } else { &__ })
			.field(if self.1 != u32::MAX { &self.1 } else { &__ })
			.finish();
	}
}

impl Default for SubNodes {
	fn default() -> Self {
		return Self(u32::MAX, u32::MAX);
	}
}
