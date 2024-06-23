#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"

/* TODO: Support malloc() backend for systems without MAP_ANON */
#ifndef MAP_ANON
#	error "System not supported (missing MAP_ANON)"
#endif

#if DEBUG
#	define TMP_DFLT_BUFSZ (8)
#else /* Probably super overkill, but that’s fine */
#	define TMP_DFLT_BUFSZ (1 * 1024)
#endif

#define MAX(x, y) ((x) > (y) ? (x) : (y))

void *
tmpalloc(scratch_t *s, size_t nmemb, size_t size)
{
	if (unlikely(size > SIZE_MAX / nmemb)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}
	size *= nmemb;

	if (s->p == NULL) {
#if !__linux__
do_mmap:
#endif
		s->sz = MAX(size, TMP_DFLT_BUFSZ);
		s->p = mmap(s->p, s->sz, PROT_READ | PROT_WRITE,
		            MAP_PRIVATE | MAP_ANON, -1, 0);
		if (s->p == MAP_FAILED)
			err("mmap:");
	} else if (size > s->sz) {
#if __linux__
		if ((s->p = mremap(s->p, s->sz, size, MREMAP_MAYMOVE)) == MAP_FAILED)
			err("mremap:");
		s->sz = size;
#else
		munmap(s->p, s->sz);
		goto do_mmap;
#endif
	}

	return s->p;
}

void
tmpfree(scratch_t *s)
{
	munmap(s->p, s->sz);
}
