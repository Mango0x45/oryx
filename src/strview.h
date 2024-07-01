#ifndef ORYX_STRVIEW_H
#define ORYX_STRVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "types.h"

typedef struct {
	const uchar *p;
	size_t len;
} strview_t;

/* Expand SV into arguments suitable for a call to printf() */
#define SV_PRI_ARGS(sv) ((int)(sv).len), ((sv).p)

/* Convert the string-literal S into a string-view */
#define SV(s)  ((strview_t){s, sizeof(s) - 1})
#define SVC(s) {s, sizeof(s) - 1}

/* Return the hash of SV */
uint64_t strview_hash(strview_t sv);


/* Copy the contents of SV to DST including a null terminator, and return DST */
uchar *svtocstr(uchar *dst, strview_t sv)
	__attribute__((returns_nonnull, nonnull));

/* Return whether or not X and Y are equal */
__attribute__((always_inline))
static inline bool
strview_eq(strview_t x, strview_t y)
{
	return x.len == y.len && memcmp(x.p, y.p, x.len) == 0;
}


#endif /* !ORYX_STRVIEW_H */
