#ifndef ORYX_UNICODE_H
#define ORYX_UNICODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUNE_C(x) UINT32_C(x)
typedef uint32_t rune;

rune utf8_decode(const char **);
size_t utf8_validate_off(const char *, size_t);
#if ORYX_SIMD
bool utf8_validate_simd(const char *, size_t);
#endif

#endif /* !ORYX_UNICODE_H */
