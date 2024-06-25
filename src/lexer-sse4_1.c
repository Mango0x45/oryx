#include <stdbool.h>
#include <stddef.h>
#include <x86intrin.h>

#include "common.h"
#include "types.h"

bool
skpcmnt(const uchar **ptr, const uchar *end)
{
	int nst = 1;
	const uchar *p = *ptr, needles[] = {'/', '*', 0, 0, 0, 0, 0, 0,
	                                    0,   0,   0, 0, 0, 0, 0, 0};
	const __m128i set = _mm_loadu_si128((const __m128i *)needles);

	while (likely(p < end)) {
		ptrdiff_t len = end - p;
		size_t blksz = MIN(len, 16);
		__m128i blk = _mm_loadu_si128((const __m128i *)p);
		int off = _mm_cmpestri(set, 2, blk, blksz, _SIDD_CMP_EQUAL_ANY);

		if (off == 16) {
			p += 16;
			continue;
		}

		if (p[off] == '*' && p[off + 1] == '/') {
			p += off + 2;
			if (--nst == 0) {
				*ptr = p;
				return true;
			}
		} else if (p[off] == '/' && p[off + 1] == '*') {
			p += off + 2;
			nst++;
		} else
			p += off + 1;
	}

	return false;
}
