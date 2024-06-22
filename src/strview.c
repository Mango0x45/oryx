#include <inttypes.h>
#include <string.h>

#include "common.h"
#include "strview.h"
#include "types.h"

uint64_t
strview_hash(strview_t sv)
{
	uint64_t h = 0x100;
	for (size_t i = 0; likely(i < sv.len); i++) {
		h ^= sv.p[i];
		h *= UINT64_C(1111111111111111111);
	}
	return h;
}

uchar *
svtocstr(uchar *dst, strview_t src)
{
	((uchar *)memcpy(dst, src.p, src.len))[src.len] = 0;
	return dst;
}
