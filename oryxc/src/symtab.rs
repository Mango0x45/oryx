#![allow(dead_code)]

use boxcar;
use dashmap::DashMap;

use crate::prelude::*;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ScopeId(pub u32);

impl ScopeId {
	pub const GLOBAL: Self = Self(0);
	pub const INVAL: Self = Self(u32::MAX);
}

#[derive(Clone, Copy, Debug)]
pub struct Scope {
	pub parent: ScopeId,
}

pub struct SymbolVal {}

pub struct SymbolTable {
	scopes:  boxcar::Vec<Scope>,
	symbols: DashMap<(ScopeId, SymbolId), SymbolVal>,
}

impl SymbolTable {
	pub fn new() -> Self {
		return Self {
            /* Initialize with the global scope */
			scopes:  boxcar::vec![Scope {
				parent: ScopeId::INVAL,
			}],
			symbols: DashMap::new(),
		};
	}

    pub fn insert(&self, scope: ScopeId, symbol: SymbolId, value: SymbolVal) {
        self.symbols.insert((scope, symbol), value);
    }
}
