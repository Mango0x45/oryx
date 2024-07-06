#ifndef ORYX_ALLOC_H
#define ORYX_ALLOC_H

#include <stdalign.h>
#include <stddef.h>

#include "common.h"

/* Callers should not modify _ARENA_DFLT_CAP.  This is included here so
   that it can be accessed from the automated tests. */
#if DEBUG
#	define _ARENA_DFLT_CAP (8)
#else
#	define _ARENA_DFLT_CAP (64 * 1024)
#endif

typedef struct _arena *arena_t;
typedef struct {
	void *p;
	size_t sz;
} scratch_t;

/* Allocate a buffer of NMEMB elements of size SIZE.  If PTR is non-null then
   reallocate the buffer it points to.  Aborts on out-of-memory or overflow. */
void *bufalloc(void *ptr, size_t nmemb, size_t size)
	__attribute__((returns_nonnull, warn_unused_result, alloc_size(2, 3)));

/* Alloc a buffer of NMEMB elements of size SIZE for one time use.  Each
   time this function is invoked previous allocations are overwritten. */
void *tmpalloc(scratch_t *s, size_t nmemb, size_t size)
	__attribute__((returns_nonnull, warn_unused_result, nonnull,
                   alloc_size(2, 3)));

/* Reallocate all memory associated with S */
void tmpfree(scratch_t *s)
	__attribute__((nonnull));

/* Allocate a buffer of NMEMB elements of size SIZE with alignment ALIGN using
   the arena-allocator A. */
void *arena_alloc(arena_t *a, size_t nmemb, size_t size, size_t align)
	__attribute__((returns_nonnull, nonnull, warn_unused_result, malloc,
                   alloc_size(2, 3), alloc_align(4)));

void *_arena_grow(arena_t *a, void *ptr, size_t old_nmemb, size_t new_nmemb,
                  size_t size, size_t align)
	__attribute__((returns_nonnull, nonnull, warn_unused_result));

typedef void *snapshot_t;
snapshot_t arena_snapshot_create(arena_t);
void arena_snapshot_restore(arena_t *, snapshot_t);

/* Deallocate all memory associated with the arena A. */
void arena_free(arena_t *a)
	__attribute__((nonnull));

/* Allocate a buffer of N elements of type T using the arena-allocator A. */
#define arena_new(a, T, n) ((T *)arena_alloc((a), (n), sizeof(T), alignof(T)))

#define arena_grow(a, p, T, on, nn)                                            \
	((T *)_arena_grow((a), (p), (on), (nn), sizeof(T), alignof(T)))

#endif /* !ORYX_ALLOC_H */
