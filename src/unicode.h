#ifndef ORYX_UNICODE_H
#define ORYX_UNICODE_H

#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "types.h"

/* Assert that CH has the Unicode Pattern_White_Space property */
bool rune_is_pat_ws(rune ch)
	__attribute__((const));

/* Assert that CH has the Unicode XID_Start or XID_Continue property.  The
   rune_is_xids() function extends XID_Start to include U+005F LOW LINE, and the
   rune_is_xidc() function extends XID_Continue to include U+2032 PRIME,
   U+2033 DOUBLE PRIME, U+2034 TRIPLE PRIME, and U+2057 QUADRUPLE PRIME. */
bool rune_is_xids(rune ch)
	__attribute__((const));
bool rune_is_xidc(rune ch)
	__attribute__((const));

/* Decode the first UTF-8 rune in S, and point S to the next rune in the stream.
   This function assumes that S points to a buffer that’s padded to a length of
   4 bytes, and doesn’t perform any form of input validation. */
rune utf8_decode(const uchar **s)
	__attribute__((nonnull));

/* Return the offset of the first invalid byte in the UTF-8 string S of length
   LEN.  This function assumes that S points to a buffer that’s padded to a
   length of 4 bytes (although LEN should not reflect any added padding). */
size_t utf8_validate_off(const uchar *s, size_t len)
	__attribute__((nonnull));

#if ORYX_SIMD
/* Assert whether the UTF-8 string S of length LEN is valid, using SIMD
   intristics to speed-up computation. */
bool utf8_validate_simd(const uchar *s, size_t len)
	__attribute__((nonnull));
#endif

#endif /* !ORYX_UNICODE_H */
