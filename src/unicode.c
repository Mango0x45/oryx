#include "unicode.h"
#include "unicode-data.h"

#define RUNE_IS_GEN(fn, stg1, stg2, blksz)                                     \
	bool fn(rune ch)                                                           \
	{                                                                          \
		unsigned x = ch % blksz;                                               \
		return stg2[stg1[ch / blksz]][x / 8] & (1 << (x % 8));                 \
	}

RUNE_IS_GEN(rune_is_pat_ws, pat_ws_stage1, pat_ws_stage2, 512)
RUNE_IS_GEN(rune_is_xids, xids_stage1, xids_stage2, 128)
RUNE_IS_GEN(rune_is_xidc, xidc_stage1, xidc_stage2, 128)

/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 *
 * Source: https://github.com/skeeto/branchless-utf8
 */

static const char lengths[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                               0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0};
static const rune mins[] = {RUNE_C(4194304), 0, 128, 2048, RUNE_C(65536)};
static const int masks[] = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
static const int shiftc[] = {0, 18, 12, 6, 0};
static const int shifte[] = {0, 6, 4, 2, 0};

rune
utf8_decode(const unsigned char **buf)
{
	const unsigned char *s = *buf;
	int len = lengths[s[0] >> 3];
	*buf = s + len + !len;

	rune c = (rune)(s[0] & masks[len]) << 18;
	c |= (rune)(s[1] & 0x3f) << 12;
	c |= (rune)(s[2] & 0x3f) << 6;
	c |= (rune)(s[3] & 0x3f) << 0;
	return c >> shiftc[len];
}

size_t
utf8_validate_off(const unsigned char *s, size_t len)
{
	const unsigned char *start = s, *end = start + len;
	while (s < end) {
		int len = lengths[s[0] >> 3];

		const unsigned char *next = s + len + !len;

		rune c = (rune)(s[0] & masks[len]) << 18;
		c |= (rune)(s[1] & 0x3f) << 12;
		c |= (rune)(s[2] & 0x3f) << 6;
		c |= (rune)(s[3] & 0x3f) << 0;
		c >>= shiftc[len];

		int e = (c < mins[len]) << 6;
		e |= ((c >> 11) == 0x1B) << 7;
		e |= (c > 0x10FFFF) << 8;
		e |= (s[1] & 0xC0) >> 2;
		e |= (s[2] & 0xC0) >> 4;
		e |= (s[3]) >> 6;
		e ^= 0x2A;
		e >>= shifte[len];
		if (e != 0)
			return s - start + 1;
		s = next;
	}

	return 0;
}
