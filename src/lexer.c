#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "errors.h"
#include "lexer.h"
#include "unicode.h"

struct lexeme *
lexstring(const char *code, size_t codesz, size_t *lcnt)
{
	struct {
		struct lexeme *p;
		size_t len, buf;
	} data = {0};

#if ORYX_SIMD
	if (!utf8_validate_simd(code, codesz)) {
#endif
		size_t off = utf8_validate_off(code, codesz);
		if (off != 0)
			err("Invalid UTF-8 at byte-offset %zu", off - 1);
#if ORYX_SIMD
	}
#endif

	const char *end = code + codesz;
	while (code < end) {
		rune ch = utf8_decode(&code);
	}

	*lcnt = data.len;
	return data.p;
}
