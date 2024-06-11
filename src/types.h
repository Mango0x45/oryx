#ifndef ORYX_TYPES_H
#define ORYX_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t      idx_t_;
typedef uint32_t      rune;
typedef unsigned char uchar;

#define RUNE_C(x) UINT32_C(x)

struct strview {
	const uchar *p;
	size_t len;
};

#endif /* !ORYX_TYPES_H */
