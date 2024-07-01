#include <sys/mman.h>

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>

#include "alloc.h"
#include "common.h"
#include "test-internal.h"

static void make_and_free(void),
            make_and_resize(void),
            free_empty_arena(void),
            make_snapshot(void);

int
main(void)
{
	make_and_free();
	make_and_resize();
	free_empty_arena();
	make_snapshot();
	return rv;
}

void
make_and_free(void)
{
	arena_t a = NULL;
	int *xs = arena_new(&a, int, 69);

	for (size_t i = 0; i < 69; i++)
		xs[i] = i;

	arena_free(&a);

	/* Assert that after arena_free(), the page was actually freed */

	unsigned char *vec = NULL;
	size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
	oryx_assert(pagesz != (size_t)-1);

	/* Vector size documented in mincore(2) */
	vec = malloc((_ARENA_DFLT_CAP + pagesz - 1) / pagesz);
	oryx_assert(vec != NULL);

	errno = 0;
	mincore(xs, sizeof(int) * 69, vec);
	oryx_assert(errno == ENOMEM);
cleanup:
	free(vec);
}

void
make_and_resize(void)
{
	arena_t a = NULL;
	size_t *xs = arena_new(&a, size_t, 100);
	for (size_t i = 0; i < 100; i++)
		xs[i] = i;

	static size_t ranges[][2] = {
		{  100,   1000},
		{ 1000,  10000},
		{10000, 100000},
	};

	for (size_t i = 0; i < lengthof(ranges); i++) {
		size_t lo = ranges[i][0];
		size_t hi = ranges[i][1];

		xs = arena_grow(&a, xs, size_t, lo, hi);
		for (size_t j = lo; j < hi; j++)
			xs[j] = j;
	}

	for (size_t i = 0; i < ranges[lengthof(ranges) - 1][1]; i++)
		oryx_assert(xs[i] == i);

cleanup:
	arena_free(&a);
}

void
free_empty_arena(void)
{
	arena_free(&(arena_t){0});
}

void
make_snapshot(void)
{
	arena_t a = NULL;

	char      *p1, *p2, *p3;
	snapshot_t s1,  s2,  s3;

	/* The arena by default is backed my mmap(), and makes pages of size
	   _ARENA_DFLT_CAP or larger.  This means we can test to see if the
	   arena snapshotting is properly implemented by:

	   1.  Creating allocations of the blocksize.  This ensures each
	       allocation is on its own page as allocated by mmap(2).
	   2.  Creating a snapshot before each allocation.
	   3.  Restoring snapshots and checking to see which pages are still
	       valid via mincore() which errors with ENOMEM if the page isn’t
		   mapped.

	   Technically mincore() is non-standard… but it exists on all major
	   BSDs as well as Linux and Darwin. */

	s1 = arena_snapshot_create(a); p1 = arena_new(&a, char, _ARENA_DFLT_CAP);
	s2 = arena_snapshot_create(a); p2 = arena_new(&a, char, _ARENA_DFLT_CAP);
	s3 = arena_snapshot_create(a); p3 = arena_new(&a, char, _ARENA_DFLT_CAP);

	unsigned char *vec = NULL;
	size_t pagesz = (size_t)sysconf(_SC_PAGESIZE);
	oryx_assert(pagesz != (size_t)-1);

	/* Vector size documented in mincore(2) */
	vec = malloc((_ARENA_DFLT_CAP + pagesz - 1) / pagesz);
	oryx_assert(vec != NULL);

	errno = 0; mincore(p1, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);
	errno = 0; mincore(p2, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);
	errno = 0; mincore(p3, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);

	arena_snapshot_restore(&a, s3);
	errno = 0; mincore(p1, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);
	errno = 0; mincore(p2, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);
	errno = 0; mincore(p3, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	oryx_assert(a != NULL);

	arena_snapshot_restore(&a, s2);
	errno = 0; mincore(p1, _ARENA_DFLT_CAP, vec); oryx_assert(errno == 0);
	errno = 0; mincore(p2, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	errno = 0; mincore(p3, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	oryx_assert(a != NULL);

	arena_snapshot_restore(&a, s1);
	errno = 0; mincore(p1, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	errno = 0; mincore(p2, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	errno = 0; mincore(p3, _ARENA_DFLT_CAP, vec); oryx_assert(errno == ENOMEM);
	oryx_assert(a == NULL);

cleanup:
	free(vec);
	arena_free(&a);
}
