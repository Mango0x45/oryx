use std::alloc::{
	self,
	Layout,
};
use std::cell::{
	Cell,
	RefCell,
};
use std::ptr::{
	self,
	NonNull,
};
use std::slice;
use std::sync::Mutex;

use crate::err;

#[derive(Copy, Clone)]
struct RawBlock {
	ptr:    NonNull<u8>,
	layout: Layout,
}

unsafe impl Send for RawBlock {}
unsafe impl Sync for RawBlock {}

pub struct GlobalArena {
	blksz:  usize,
	blocks: Mutex<Vec<RawBlock>>,
}

impl GlobalArena {
	pub fn new(blksz: usize) -> Self {
		Self {
			blksz,
			blocks: Mutex::new(Vec::new()),
		}
	}

	fn allocate_block(&self, layout: Layout) -> RawBlock {
		let layout = Layout::from_size_align(
			layout.size().max(self.blksz),
			layout.align().max(16),
		)
		.unwrap_or_else(|e| err!(e, "allocation error"));
		let ptr = NonNull::new(unsafe { alloc::alloc(layout) })
			.unwrap_or_else(|| alloc::handle_alloc_error(layout));

		let block = RawBlock { ptr, layout };
		self.blocks.lock().unwrap().push(block.clone());
		return block;
	}
}

impl Drop for GlobalArena {
	fn drop(&mut self) {
		for block in self.blocks.lock().unwrap().iter() {
			unsafe {
				alloc::dealloc(block.ptr.as_ptr(), block.layout);
			}
		}
	}
}

#[derive(Clone, Copy)]
pub struct Mark {
	blk: usize,
	beg: *mut u8,
	end: *mut u8,
}

pub struct LocalArena<'a> {
	parent:        &'a GlobalArena,
	beg:           Cell<*mut u8>,
	end:           Cell<*mut u8>,
	curblk:        Cell<usize>,
	blks:          RefCell<Vec<RawBlock>>,
	#[cfg(debug_assertions)]
	pub waterline: Cell<usize>,
}

impl<'a> LocalArena<'a> {
	pub fn new(parent: &'a GlobalArena) -> Self {
		return Self {
			parent,
			beg: Cell::new(ptr::null_mut()),
			end: Cell::new(ptr::null_mut()),
			curblk: Cell::new(usize::MAX),
			blks: RefCell::new(Vec::new()),
			#[cfg(debug_assertions)]
			waterline: Cell::new(0),
		};
	}

	fn alloc_raw(&self, layout: Layout) -> NonNull<u8> {
		if cfg!(debug_assertions) {
			self.waterline.update(|x| x + layout.size());
		}

		let beg = self.beg.get() as usize;

		let align_offset =
			beg.wrapping_add(layout.align() - 1) & !(layout.align() - 1);
		let Some(next_beg) = align_offset.checked_add(layout.size()) else {
			err!("allocation overflow");
		};

		return if next_beg <= self.end.get() as usize {
			self.beg.set(next_beg as *mut u8);
			unsafe { NonNull::new_unchecked(align_offset as *mut u8) }
		} else {
			self.alloc_slow(layout)
		};
	}

	#[cold]
	fn alloc_slow(&self, layout: Layout) -> NonNull<u8> {
		let mut blks = self.blks.borrow_mut();
		let next_blk = self.curblk.get().wrapping_add(1);

		/* Find the next recycleable block that fits */
		let mut found_blk = None;
		let mut align_beg = 0;
		for i in next_blk..blks.len() {
			let blk = &blks[i];
			let beg = blk.ptr.as_ptr() as usize;
			let end = beg + blk.layout.size();
			let aligned =
				beg.wrapping_add(layout.align() - 1) & !(layout.align() - 1);
			if aligned.checked_add(layout.size()).unwrap_or(usize::MAX) <= end {
				found_blk = Some(i);
				align_beg = aligned;
				break;
			}
		}

		if let Some(i) = found_blk {
			blks.swap(next_blk, i);

			let blk = &blks[next_blk];
			let end = blk.ptr.as_ptr() as usize + blk.layout.size();

			self.curblk.set(next_blk);
			self.beg.set((align_beg + layout.size()) as *mut u8);
			self.end.set(end as *mut u8);

			return unsafe { NonNull::new_unchecked(align_beg as *mut u8) };
		}

		let blk = self.parent.allocate_block(layout);
		let beg = blk.ptr.as_ptr() as usize;
		let end = beg + blk.layout.size();
		let align_beg =
			beg.wrapping_add(layout.align() - 1) & !(layout.align() - 1);

		blks.push(blk);
		let last_blk = blks.len() - 1;
		blks.swap(next_blk, last_blk);

		self.curblk.set(next_blk);
		self.beg.set((align_beg + layout.size()) as *mut u8);
		self.end.set(end as *mut u8);

		unsafe { NonNull::new_unchecked(align_beg as *mut u8) }
	}

	pub fn alloc<T>(&self, value: T) -> &'a mut T {
		let ptr = self.alloc_raw(Layout::new::<T>()).cast::<T>().as_ptr();
		unsafe {
			ptr::write(ptr, value);
			return &mut *ptr;
		}
	}

	pub fn alloc_slice<T>(&self, len: usize) -> &'a mut [T] {
		let layout = Layout::array::<T>(len)
			.unwrap_or_else(|e| err!(e, "allocation error"));
		let ptr = self.alloc_raw(layout).cast::<T>().as_ptr();
		return unsafe { slice::from_raw_parts_mut(ptr, len) };
	}

	pub fn mark(&self) -> Mark {
		return Mark {
			blk: self.curblk.get(),
			beg: self.beg.get(),
			end: self.end.get(),
		};
	}

	pub fn restore(&self, mark: Mark) {
		self.curblk.set(mark.blk);
		self.beg.set(mark.beg);
		self.end.set(mark.end);
	}

	pub fn scope<'s, R, F>(&'s mut self, f: F) -> R
	where
		F: FnOnce(&ScopedArena<'s, 'a>) -> R,
	{
		let m = self.mark();
		let r = f(&ScopedArena { inner: self });
		self.restore(m);
		return r;
	}
}

/// A wrapper around LocalArena that bounds returned references to 's.
pub struct ScopedArena<'s, 'a> {
	inner: &'s LocalArena<'a>,
}

impl<'s, 'a> ScopedArena<'s, 'a> {
	pub fn alloc<T>(&self, value: T) -> &'s mut T {
		return self.inner.alloc(value);
	}

	pub fn alloc_slice<T>(&self, len: usize) -> &'s mut [T] {
		return self.inner.alloc_slice(len);
	}
}

#[test]
fn test_alloc_slice() {
	let arena_global = GlobalArena::new(8);
	let arena_local_1 = LocalArena::new(&arena_global);
	let arena_local_2 = LocalArena::new(&arena_global);

	let s1 = arena_local_1.alloc_slice(8);
	let s2 = arena_local_2.alloc_slice(4);
	assert_eq!(s1.len(), 8);
	assert_eq!(s2.len(), 4);

	for i in 0..s1.len() {
		s1[i] = i;
	}
	for i in 0..s2.len() {
		s2[i] = i;
	}
}

#[test]
fn test_arena_grows() {
	let arena_global = GlobalArena::new(8);
	let arena_local = LocalArena::new(&arena_global);

	let s1 = arena_local.alloc_slice(8);
	let s2 = arena_local.alloc_slice(69);
	assert_eq!(s1.len(), 8);
	assert_eq!(s2.len(), 69);

	for i in 0..s1.len() {
		s1[i] = i;
	}
	for i in 0..s2.len() {
		s2[i] = i;
	}
}
