#ifndef ORYX_TYPES_H
#define ORYX_TYPES_H

#include <stddef.h>

typedef unsigned char uchar;

struct strview {
	const uchar *p;
	size_t len;
};

#endif /* !ORYX_TYPES_H */
