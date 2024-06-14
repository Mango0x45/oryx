#ifndef ORYX_ALLOC_H
#define ORYX_ALLOC_H

#include <stdalign.h>
#include <stddef.h>

#include "common.h"

typedef struct _arena *arena;

/* Allocate a buffer of NMEMB elements of size SIZE.  If PTR is non-null then
   reallocate the buffer it points to.  Aborts on out-of-memory or overflow. */
void *bufalloc(void *ptr, size_t nmemb, size_t size)
	__attribute__((returns_nonnull, warn_unused_result, alloc_size(2, 3)));

/* Allocate a buffer of NMEMB elements of size SIZE with alignment ALIGN using
   the arena-allocator A. */
void *arena_alloc(arena *a, size_t nmemb, size_t size, size_t align)
	__attribute__((returns_nonnull, warn_unused_result, malloc,
                   alloc_size(2, 3), alloc_align(4)));

/* Deallocate all memory associated with the arena A. */
void arena_free(arena *a)
	__attribute__((nonnull));

/* Allocate a buffer of N elements of type T using the arena-allocator A. */
#define arena_new(a, T, n) ((T *)arena_alloc((a), (n), sizeof(T), alignof(T)))

#endif /* !ORYX_ALLOC_H */
