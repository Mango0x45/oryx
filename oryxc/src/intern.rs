use std::hash::{
	Hash,
	Hasher,
};

use dashmap::DashMap;
use unicode_normalization::{
	self,
	IsNormalized,
	UnicodeNormalization,
};

// use icu::normalizer::DecomposingNormalizer;
use crate::prelude::*;

pub struct Interner<'a> {
	map:   DashMap<UniStr<'a>, SymbolId>,
	store: Vec<&'a str>,
}

#[derive(Debug, Eq)]
pub struct UniStr<'a>(pub &'a str);

impl Hash for UniStr<'_> {
	fn hash<H: Hasher>(&self, state: &mut H) {
		/* In the ASCII common case we use .bytes() to avoid decoding
		 * every codepoint (a no-op in ASCII) */
		if self.0.is_ascii() {
			self.0.bytes().for_each(|c| (c as char).hash(state));
		} else if unicode_normalization::is_nfkd_quick(self.0.chars())
			== IsNormalized::Yes
		{
			self.0.chars().for_each(|c| c.hash(state));
		} else {
			self.0.nfkd().for_each(|c| c.hash(state));
		}
	}
}

impl PartialEq for UniStr<'_> {
	fn eq(&self, other: &Self) -> bool {
		/* Most code is ASCII, and normalization is obviously a lot
		 * slower than not normalizing, so we try to only normalize when
		 * we have to */
		return match (
			unicode_normalization::is_nfkd_quick(self.0.chars())
				== IsNormalized::Yes,
			unicode_normalization::is_nfkd_quick(other.0.chars())
				== IsNormalized::Yes,
		) {
			(true, true) => self.0 == other.0,
			(true, false) => {
				self.0.bytes().map(|b| b as char).eq(other.0.nfkd())
			},
			(false, true) => {
				self.0.nfkd().eq(other.0.bytes().map(|b| b as char))
			},
			(false, false) => self.0.nfkd().eq(other.0.nfkd()),
		};
	}
}

impl<'a> Interner<'a> {
	pub fn new() -> Self {
		return Interner {
			map:   DashMap::new(),
			store: Vec::new(),
		};
	}

	pub fn get(&self, key: SymbolId) -> &str {
		return self.store[key.0 as usize];
	}

	pub fn intern(&mut self, value: &'a str) -> SymbolId {
		if let Some(key) = self.map.get(&UniStr(value)) {
			return *key;
		}
		let key = SymbolId(self.store.len() as u32);
		self.map.insert(UniStr(value), key);
		self.store.push(value);
		return key;
	}
}

#[test]
fn test_unistr_eq() {
	assert_eq!(UniStr("fishi"), UniStr("ﬁshᵢ"));
	assert_eq!(UniStr("fishi"), UniStr("fishi"));
	assert_eq!(UniStr("ﬁshi"), UniStr("fishᵢ"));
	assert_eq!(UniStr("ﬁshᵢ"), UniStr("ﬁshᵢ"));
}

#[test]
fn test_unistr_hash() {
	use std::hash::DefaultHasher;
	for (lhs, rhs) in &[
		(UniStr("fishi"), UniStr("ﬁshᵢ")),
		(UniStr("fishi"), UniStr("fishi")),
		(UniStr("ﬁshi"), UniStr("fishᵢ")),
		(UniStr("ﬁshᵢ"), UniStr("ﬁshᵢ")),
	] {
		let mut hashl = DefaultHasher::new();
		let mut hashr = DefaultHasher::new();
		lhs.hash(&mut hashl);
		rhs.hash(&mut hashr);
		assert_eq!(hashl.finish(), hashr.finish());
	}
}

#[test]
fn test_interner_intern() {
	let xs = ["ﬁshi", "fishi", "ﬁshᵢ"];
	let y = "andy";

	let mut interner = Interner::new();
	for i in 0..xs.len() {
		for j in i..xs.len() {
			assert_eq!(interner.intern(xs[i]), interner.intern(xs[j]));
		}
	}
	for i in 0..xs.len() {
		assert_ne!(interner.intern(y), interner.intern(xs[i]));
	}
}

#[test]
fn test_interner_gets_first_inserted() {
	let mut interner = Interner::new();
	let xs = ["ﬁshi", "fishi", "ﬁshᵢ"];
	let ys = xs.iter().map(|x| interner.intern(x)).collect::<Vec<_>>();

	for i in 0..ys.len() {
		assert_eq!(interner.get(ys[i]), xs[0]);
	}
}
