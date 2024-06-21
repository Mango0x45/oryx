#ifndef ORYX_STRVIEW_H
#define ORYX_STRVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "types.h"

struct strview {
	const uchar *p;
	size_t len;
};

/* Expand SV into arguments suitable for a call to printf() */
#define SV_PRI_ARGS(sv) ((int)(sv).len), ((sv).p)

/* Convert the string-literal S into a string-view */
#define SV(s) ((struct strview){s, sizeof(s) - 1})

/* Return the hash of SV */
uint64_t strview_hash(struct strview sv);


/* Copy the contents of SV to DST including a null terminator, and return DST */
uchar *svtocstr(uchar *dst, struct strview sv)
	__attribute__((returns_nonnull, nonnull));

/* Return whether or not X and Y are equal */
__attribute__((always_inline))
static inline bool
strview_eq(struct strview x, struct strview y)
{
	return x.len == y.len && memcmp(x.p, y.p, x.len) == 0;
}


#endif /* !ORYX_STRVIEW_H */
