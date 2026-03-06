use std::alloc::{
	self,
	Layout,
};
use std::cell::Cell;
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
			layout.align(),
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

pub struct LocalArena<'a> {
	parent: &'a GlobalArena,
	cursor: Cell<*mut u8>,
	end:    Cell<*mut u8>,
}

impl<'a> LocalArena<'a> {
	pub fn new(parent: &'a GlobalArena) -> Self {
		return Self {
			parent,
			cursor: Cell::new(ptr::null_mut()),
			end: Cell::new(ptr::null_mut()),
		};
	}

	fn alloc_raw(&self, layout: Layout) -> NonNull<u8> {
		let cursor = self.cursor.get() as usize;

		let align_offset =
			cursor.wrapping_add(layout.align() - 1) & !(layout.align() - 1);
		let Some(next_cursor) = align_offset.checked_add(layout.size()) else {
			err!("allocation overflow");
		};

		return if next_cursor <= self.end.get() as usize {
			self.cursor.set(next_cursor as *mut u8);
			unsafe { NonNull::new_unchecked(align_offset as *mut u8) }
		} else {
			self.alloc_slow(layout)
		};
	}

	#[cold]
	fn alloc_slow(&self, layout: Layout) -> NonNull<u8> {
		let block = self.parent.allocate_block(layout);
		let beg = block.ptr.as_ptr() as usize;
		let end = beg + block.layout.size();
		let next_cursor = beg + layout.size();
		self.cursor.set(next_cursor as *mut u8);
		self.end.set(end as *mut u8);
		return unsafe { NonNull::new_unchecked(beg as *mut u8) };
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
