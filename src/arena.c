#include <sys/mman.h>

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "alloc.h"
#include "errors.h"

/* TODO: Support implementations without MAP_ANON? */
#ifndef MAP_ANON
static_assert(NULL, "MAP_ANON not available on this system");
#endif

#if DEBUG
#	define ARENA_DFLT_CAP (8)
#else
#	define ARENA_DFLT_CAP (2048)
#endif

#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define IS_POW_2(n) ((n) != 0 && ((n) & ((n)-1)) == 0)

struct _arena {
	size_t len, cap;
	void *data, *last;
	struct _arena *next;
};

static struct _arena *mkblk(size_t) __attribute__((returns_nonnull));
static inline size_t pad(size_t, size_t) __attribute__((const, always_inline));

void *
arena_alloc(struct _arena **a, size_t nmemb, size_t size, size_t align)
{
	assert(IS_POW_2(align));
	assert(nmemb * size != 0);

	if (size > SIZE_MAX / nmemb) {
		errno = EOVERFLOW;
		err("%s:", __func__);
	}

	size *= nmemb;

	for (struct _arena *p = *a; p != NULL; p = p->next) {
		size_t nlen, off;
		off = pad(p->len, align);
		nlen = size + off;

		if (nlen <= p->cap) {
			void *ret = (char *)p->data + off;
			p->len = nlen;
			p->last = ret;
			return ret;
		}
	}

	/* No page exists with enough space */
	struct _arena *p = mkblk(MAX(size, ARENA_DFLT_CAP));
	p->len = size;
	p->next = *a;
	*a = p;
	return p->data;
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

struct _arena *
mkblk(size_t cap)
{
	struct _arena *a = malloc(sizeof(*a));
	if (a == NULL)
		err("malloc:");
	a->cap = cap;
	a->data = mmap(NULL, cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (a->data == MAP_FAILED)
		err("mmap:");
	a->last = a->data;
	return a;
}

size_t
pad(size_t len, size_t align)
{
	return (len + align - 1) & ~(align - 1);
}
