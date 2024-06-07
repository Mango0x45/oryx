#include "unicode.h"

/* Branchless UTF-8 decoding and validation by Christopher Wellons.

   You can find the original source with comments at
   https://github.com/skeeto/branchless-utf8. */

static const char lengths[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                               0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0};
static const rune mins[] = {RUNE_C(4194304), 0, 128, 2048, RUNE_C(65536)};
static const int masks[] = {0x00, 0x7f, 0x1f, 0x0f, 0x07};
static const int shiftc[] = {0, 18, 12, 6, 0};
static const int shifte[] = {0, 6, 4, 2, 0};

rune
utf8_decode(const char **buf)
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
utf8_validate_off(const char *buf, size_t len)
{
	const char *start = buf, *end = start + len;
	while (buf < end) {
		const unsigned char *s = buf;
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
			return buf - start + 1;
		buf = next;
	}

	return 0;
}
