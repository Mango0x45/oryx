#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "errors.h"
#include "lexer.h"
#include "unicode.h"

static bool skip_comment(const unsigned char **, const char *);

struct lexeme *
lexstring(const unsigned char *code, size_t codesz, size_t *lcnt)
{
	struct {
		struct lexeme *buf;
		size_t len, cap;
	} data = {.cap = 1024};
	if ((data.buf = malloc(data.cap)) == NULL)
		err("malloc:");

#if ORYX_SIMD
	if (!utf8_validate_simd(code, codesz)) {
#endif
		size_t loc = utf8_validate_off(code, codesz);
		if (loc != 0) {
			err("Invalid byte ‘0x%02" PRIx8 "’ in UTF-8 input at byte %zu",
			    code[loc - 1], loc);
		}
#if ORYX_SIMD
	}
#endif

	const unsigned char *start = code, *end = start + codesz;
	while (code < end) {
		struct lexeme l;
		const unsigned char *spnbeg = code, *spnend;
		rune ch = utf8_decode(&code);

		switch (ch) {
		/* Single-byte literals */
		case '&': case '(': case ')': case '*':
		case '+': case '-': case ':': case '=':
		case ';': case '{': case '|': case '}':
		case '~':
			l.kind = ch;
			break;

		/* Single- or double-byte literals */
		case '/':
			if (code < end && code[0] == '*') {
				if (!skip_comment(&code, end))
					err("Unterminated comment at byte %td", code - start);
				continue;
			}

			l.kind = ch;
			break;

		case '<':
		case '>':
			l.kind = ch;

			/* See the comment in lexer.h for where 193 comes from */
			if (code < end && code[0] == ch) {
				code++;
				l.kind += 193;
			}
			break;

		default:
			if (!rune_is_xids(ch))
				continue;

			l.kind = LEXIDENT;
			l.p = spnbeg;

			spnend = code;
			while (code < end && rune_is_xidc(ch)) {
				spnend = code;
				ch = utf8_decode(&code);
			}
			if (code < end)
				code = spnend;

			l.len = spnend - spnbeg;
		}

		if (data.len == data.cap) {
			data.cap *= 2;
			if ((data.buf = realloc(data.buf, data.cap)) == NULL)
				err("realloc:");
		}

		data.buf[data.len++] = l;
	}

	*lcnt = data.len;
	return data.buf;
}

bool
skip_comment(const unsigned char **ptr, const char *end)
{
	int nst = 1;
	const char *p = *ptr;

	for (p++; p < end; p++) {
		if (p + 1 < end) {
			if (p[0] == '*' && p[1] == '/') {
				p++;
				if (--nst == 0)
					goto out;
			} else if (p[0] == '/' && p[1] == '*') {
				p++;
				nst++;
			}
		}
	}

	return false;

out:
	*ptr = ++p;
	return true;
}
