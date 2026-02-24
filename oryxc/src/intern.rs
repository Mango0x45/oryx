use std::hash;

use dashmap;
use icu::normalizer;

#[repr(transparent)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Key(u32);

pub struct Interner<'a> {
	map:   dashmap::DashMap<UniStr<'a>, Key>,
	store: Vec<&'a str>,
}

#[derive(Eq)]
pub struct UniStr<'a>(pub &'a str);

impl hash::Hash for UniStr<'_> {
	fn hash<H: hash::Hasher>(&self, state: &mut H) {
		if self.0.is_ascii() {
            self.0.chars().for_each(|c| c.hash(state));
		} else {
			let nfkd = normalizer::DecomposingNormalizer::new_nfkd();
			nfkd.normalize_iter(self.0.chars()).for_each(|c| c.hash(state));
		}
	}
}

impl PartialEq for UniStr<'_> {
	fn eq(&self, other: &Self) -> bool {
		let nfkd = normalizer::DecomposingNormalizer::new_nfkd();
		return match (self.0.is_ascii(), other.0.is_ascii()) {
			(true, true) => self.0 == other.0,
			(true, false) => {
				self.0.chars().eq(nfkd.normalize_iter(other.0.chars()))
			},
			(false, true) => {
				other.0.chars().eq(nfkd.normalize_iter(self.0.chars()))
			},
			(false, false) => nfkd
				.normalize_iter(self.0.chars())
				.eq(nfkd.normalize_iter(other.0.chars())),
		};
	}
}

impl<'a> Interner<'a> {
	pub fn new() -> Self {
		return Interner {
			map:   dashmap::DashMap::new(),
			store: Vec::new(),
		};
	}

	pub fn get(&self, key: Key) -> &str {
		return self.store[key.0 as usize];
	}

	pub fn intern(&mut self, value: &'a str) -> Key {
		if let Some(key) = self.map.get(&UniStr(value)) {
			return *key;
		}
		let key = Key(self.store.len() as u32);
		self.map.insert(UniStr(value), key);
		self.store.push(value);
		return key;
	}
}
