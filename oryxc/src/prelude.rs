use std::fmt::{
	self,
	Debug,
	Formatter,
};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct FileId(pub usize);

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(pub usize);

impl ScopeId {
	pub const GLOBAL: Self = Self(0);
}

#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct SymbolId(pub u32);

pub struct SymbolVal {}

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
