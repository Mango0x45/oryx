#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"

/* TODO: Support malloc() backend for systems without MAP_ANON */
#ifndef MAP_ANON
#	error "System not supported (missing MAP_ANON)"
#endif

#if DEBUG
#	define ARENA_DFLT_CAP (8)
#else
#	define ARENA_DFLT_CAP (2048)
#endif

#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define IS_POW_2(n) ((n) != 0 && ((n) & ((n)-1)) == 0)

struct _arena {
	/* DATA points to the start of the block’s memory while FREE points
	   to the beginning of the unused data in the block */
	void *data, *free;
	size_t cap;
	struct _arena *next;
};

/* Return a new arena block of size SZ */
static struct _arena *mkblk(size_t sz)
	__attribute__((returns_nonnull));

/* Return the padding required to properly align an allocation with
   alignment ALIGN at offset OFF */
static inline size_t pad(size_t off, size_t align)
	__attribute__((const, always_inline));

void *
arena_alloc(struct _arena **a, size_t nmemb, size_t size, size_t align)
{
	assert(IS_POW_2(align));
	assert(nmemb * size != 0);

	if (unlikely(size > SIZE_MAX / nmemb)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}

	size *= nmemb;

	for (struct _arena *p = *a; p != NULL; p = p->next) {
		size_t padding = pad((char *)p->free - (char *)p->data, align);
		size_t freespc = p->cap - ((char *)p->free - (char *)p->data);
		size_t nsize   = size + padding;

		if (nsize <= freespc) {
			void *ret = p->free;
			p->free = (char *)p->free + nsize;
			return ret;
		}
	}

	/* No page exists with enough space */
	struct _arena *p = mkblk(MAX(size, ARENA_DFLT_CAP));
	p->next = *a;
	*a = p;
	p->free = (char *)p->data + size;
	return p->data;
}

void *
_arena_grow(arena_t *a, void *ptr, size_t old_nmemb, size_t new_nmemb,
            size_t size, size_t align)
{
	assert(IS_POW_2(align));
	assert(new_nmemb * size != 0);
	assert(old_nmemb < new_nmemb);

	if (unlikely(size > SIZE_MAX / new_nmemb)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}

	for (struct _arena *p = *a; p != NULL; p = p->next) {
		if (ptr < p->data || ptr > p->free)
			continue;

		/* If we need to grow the given allocation, but it was the last
		   allocation made in a region, then we first see if we can just eat
		   more trailing free space in the region to avoid a memcpy(). */
		size_t oldsz = old_nmemb * size;
		if ((char *)ptr == (char *)p->free - oldsz) {
			size_t rest = p->cap - ((char *)p->free - (char *)p->data);
			size_t need = (new_nmemb - old_nmemb) * size;
			if (need <= rest) {
				p->free = (char *)p->free + need;
				return ptr;
			}
		}

		void *dst = arena_alloc(a, new_nmemb, size, align);
		return memcpy(dst, ptr, old_nmemb * size);
	}

#if DEBUG
	err("%s:%d: tried to resize pointer that wasn’t allocated", __func__,
	    __LINE__);
#else
	__builtin_unreachable();
#endif
}

void
arena_free(struct _arena **a)
{
	struct _arena *cur, *next;
	for (cur = *a; cur != NULL; cur = next) {
		next = cur->next;
		munmap(cur->data, cur->cap);
		free(cur);
	}
	*a = NULL;
}

snapshot_t
arena_snapshot_create(struct _arena *a)
{
	return a == NULL ? NULL : a->free;
}

void
arena_snapshot_restore(struct _arena **a, snapshot_t snp)
{
	if (snp == NULL) {
		arena_free(a);
		return;
	}

	struct _arena *cur, *next;
	for (cur = *a; cur != NULL; cur = next) {
		next = cur->next;
		if (snp < cur->data || snp > cur->free) {
			munmap(cur->data, cur->cap);
			free(cur);
		} else {
			cur->free = snp;
			*a = cur;
			return;
		}
	}
}

struct _arena *
mkblk(size_t cap)
{
	struct _arena *a = malloc(sizeof(*a));
	if (a == NULL)
		err("malloc:");
	a->cap = cap;
	a->data = a->free = mmap(NULL, cap, PROT_READ | PROT_WRITE,
	                         MAP_PRIVATE | MAP_ANON, -1, 0);
	if (a->data == MAP_FAILED)
		err("mmap:");
	return a;
}

size_t
pad(size_t len, size_t align)
{
	return (len + align - 1) & ~(align - 1);
}
