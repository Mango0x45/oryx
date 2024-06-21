#include <string.h>

#include "strview.h"
#include "types.h"

uint64_t
strview_hash(struct strview sv)
{
	uint64_t h = 0x100;
	for (size_t i = 0; i < sv.len; i++) {
		h ^= sv.p[i];
		h *= 1111111111111111111u;
	}
	return h;
}

uchar *
svtocstr(uchar *dst, struct strview src)
{
	memcpy(dst, src.p, src.len);
	dst[src.len] = 0;
	return dst;
}
