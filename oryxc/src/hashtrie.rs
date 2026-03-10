use std::hash::{
	Hash,
	Hasher,
};
use std::ptr;
use std::sync::atomic::{
	AtomicPtr,
	Ordering,
};

use crate::arena::LocalArena;

struct FNV1a {
	state: u64,
}

impl FNV1a {
	const OFFSET_BASIS: u64 = 0xCBF29CE484222325;
	const PRIME: u64 = 0x00000100000001B3;

	fn new() -> Self {
		return Self {
			state: Self::OFFSET_BASIS,
		};
	}
}

impl Hasher for FNV1a {
	fn write(&mut self, bytes: &[u8]) {
		for &b in bytes {
			self.state ^= b as u64;
			self.state = self.state.wrapping_mul(Self::PRIME);
		}
	}

	fn finish(&self) -> u64 {
		return self.state;
	}
}

pub struct HTrieNode<K, V>
where
	K: Copy + Eq + Hash,
{
	sub: [AtomicPtr<HTrieNode<K, V>>; 4],
	key: K,
	val: AtomicPtr<V>,
}

impl<K, V> HTrieNode<K, V>
where
	K: Copy + Eq + Hash,
{
	fn new(key: K, valptr: *mut V) -> Self {
		return Self {
			sub: [
				AtomicPtr::new(ptr::null_mut()),
				AtomicPtr::new(ptr::null_mut()),
				AtomicPtr::new(ptr::null_mut()),
				AtomicPtr::new(ptr::null_mut()),
			],
			key,
			val: AtomicPtr::new(valptr),
		};
	}
}

#[derive(Debug)]
pub struct HTrie<K, V>
where
	K: Copy + Eq + Hash,
{
	root: AtomicPtr<HTrieNode<K, V>>,
}

impl<K, V> HTrie<K, V>
where
	K: Copy + Eq + Hash,
{
	pub fn new() -> Self {
		return Self {
			root: AtomicPtr::new(ptr::null_mut()),
		};
	}

	/// Lock-free insert into the hash-trie.
	///
	/// Returns `None` if the key was newly inserted.
	/// Returns `Some(val)` with the previous value if the key already
	/// existed.
	pub fn insert(&self, key: K, val: V, arena: &LocalArena) -> Option<V> {
		let mut h = {
			let mut h = FNV1a::new();
			key.hash(&mut h);
			h.finish()
		};

		let mut m = &self.root;
		let valptr = arena.alloc(val);

		loop {
			let mut n = m.load(Ordering::Acquire);

			if n.is_null() {
				let mark = arena.mark();
				let node = arena.alloc(HTrieNode::new(key, valptr));

				match m.compare_exchange(
					ptr::null_mut(),
					node as *mut HTrieNode<K, V>,
					Ordering::Release,
					Ordering::Acquire,
				) {
					Ok(_) => {
						return None;
					},
					Err(ptr) => {
						arena.restore(mark);
						n = ptr;
					},
				}
			}

			let n = unsafe { &*n };

			if n.key == key {
				let old_valptr = n.val.swap(valptr, Ordering::AcqRel);

				if old_valptr.is_null() {
					return None;
				} else {
					let old_val = unsafe { ptr::read(old_valptr) };
					return Some(old_val);
				}
			}

			m = &n.sub[(h >> 62) as usize];
			h <<= 2;
		}
	}

	/// Check if the given key exists in the hash-trie.
	pub fn contains(&self, key: K) -> bool {
		let mut h = {
			let mut h = FNV1a::new();
			key.hash(&mut h);
			h.finish()
		};

		let mut m = &self.root;

		loop {
			let n = m.load(Ordering::Acquire);
			if n.is_null() {
				return false;
			}
			let n = unsafe { &*n };
			if n.key == key {
				return !n.val.load(Ordering::Acquire).is_null();
			}
			m = &n.sub[(h >> 62) as usize];
			h <<= 2;
		}
	}
}

#[cfg(test)]
mod tests {
	use crate::arena::{
		GlobalArena,
		LocalArena,
	};
	use crate::hashtrie::HTrie;

	#[test]
	fn test_htrie_insert() {
		let ga = GlobalArena::new(128);
		let la = LocalArena::new(&ga);
		let ht = HTrie::new();

		assert_eq!(ht.insert("foo", "bar", &la), None);
		assert_eq!(ht.insert("hello", "sailor", &la), None);
		assert_eq!(ht.insert("thomas", "voss", &la), None);
	}

	#[test]
	fn test_htrie_overwrite() {
		let ga = GlobalArena::new(128);
		let la = LocalArena::new(&ga);
		let ht = HTrie::new();

		ht.insert("foo", "bar", &la);
		ht.insert("hello", "sailor", &la);
		ht.insert("thomas", "voss", &la);
		assert_eq!(ht.insert("foo", "bar-0", &la), Some("bar"));
		assert_eq!(ht.insert("hello", "sailor-0", &la), Some("sailor"));
		assert_eq!(ht.insert("thomas", "voss-0", &la), Some("voss"));
		assert_eq!(ht.insert("foo", "bar-1", &la), Some("bar-0"));
		assert_eq!(ht.insert("hello", "sailor-1", &la), Some("sailor-0"));
		assert_eq!(ht.insert("thomas", "voss-1", &la), Some("voss-0"));
	}

	#[test]
	fn test_htrie_contains() {
		let ga = GlobalArena::new(128);
		let la = LocalArena::new(&ga);
		let ht = HTrie::new();

		assert!(!ht.contains("foo"));
		assert!(!ht.contains("hello"));
		assert!(!ht.contains("thomas"));
		ht.insert("foo", "bar", &la);
		ht.insert("hello", "sailor", &la);
		ht.insert("thomas", "voss", &la);
		assert!(ht.contains("foo"));
		assert!(ht.contains("hello"));
		assert!(ht.contains("thomas"));
	}
}
