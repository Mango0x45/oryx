#ifndef ORYX_TYPES_H
#define ORYX_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned char uchar;
typedef uint32_t      idx_t_;

struct strview {
	const uchar *p;
	size_t len;
};

#endif /* !ORYX_TYPES_H */
