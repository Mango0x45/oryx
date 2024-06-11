#ifndef ORYX_ALLOC_H
#define ORYX_ALLOC_H

#include <stddef.h>

/* Allocate a buffer of NMEMB elements of size SIZE.  If PTR is non-null then
   reallocate the buffer it points to.  Aborts on out-of-memory or overflow. */
void *bufalloc(void *ptr, size_t nmemb, size_t size)
	__attribute__((returns_nonnull));

#endif /* !ORYX_ALLOC_H */
