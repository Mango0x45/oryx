#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "lexer.h"
#include "unicode.h"

#ifdef __GNUC__
#	define likely(x)   __builtin_expect(!!(x), 1)
#	define unlikely(x) __builtin_expect(!!(x), 0)
#else
#	define likely(x)   (x)
#	define unlikely(x) (x)
#endif

#define SIZE_WDTH (sizeof(size_t) * 8)

static bool skip_comment(const uchar **, const uchar *);

static struct lexemes_soa mk_lexemes_soa(void);
static void lexemes_soa_resz(struct lexemes_soa *);

struct lexemes_soa
lexstring(const uchar *code, size_t codesz)
{
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

	struct lexemes_soa data = mk_lexemes_soa();

	const uchar *start = code, *end = start + codesz;
	while (likely(code < end)) {
		const uchar *spnbeg = code, *spnend;
		rune ch = utf8_decode(&code);

		switch (ch) {
		/* Single-byte literals */
		case '&':
		case '(':
		case ')':
		case '*':
		case '+':
		case '-':
		case ':':
		case ';':
		case '=':
		case '[':
		case ']':
		case '{':
		case '|':
		case '}':
		case '~':
			data.kinds[data.len++] = ch;
			break;

		/* Single- or double-byte literals */
		case '/':
			if (code < end && code[0] == '*') {
				if (!skip_comment(&code, end))
					err("Unterminated comment at byte %td", code - start);
				continue;
			}

			data.kinds[data.len++] = ch;
			break;

		case '<':
		case '>':
			data.kinds[data.len++] = ch;

			/* See the comment in lexer.h for where 193 comes from */
			if (code < end && code[0] == ch) {
				code++;
				data.kinds[data.len - 1] += 193;
			}
			break;

		default:
			if (!rune_is_xids(ch))
				continue;

			data.kinds[data.len] = LEXIDENT;
			data.strs[data.len].p = spnbeg;

			spnend = code;
			while (likely(code < end) && rune_is_xidc(ch)) {
				spnend = code;
				ch = utf8_decode(&code);
			}
			if (likely(code < end))
				code = spnend;

			data.strs[data.len++].len = spnend - spnbeg;
		}

		if (unlikely(data.len == data.cap))
			lexemes_soa_resz(&data);
	}

	return data;
}

bool
skip_comment(const uchar **ptr, const uchar *end)
{
	int nst = 1;
	const uchar *p = *ptr;

	for (p++; likely(p < end); p++) {
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

struct lexemes_soa
mk_lexemes_soa(void)
{
	static_assert(offsetof(struct lexemes_soa, kinds)
	                  < offsetof(struct lexemes_soa, strs),
	              "KINDS is not the first field before STRS");

	struct lexemes_soa soa;
	soa.len = 0;
	soa.cap = 2048;

	/* Ensure that soa.strs is properly aligned */
	size_t pad = alignof(*soa.strs)
	           - soa.cap * sizeof(*soa.kinds) % alignof(*soa.strs);
	if (pad == 8)
		pad = 0;

	if ((soa.kinds = malloc(soa.cap * LEXEMES_SOA_BLKSZ + pad)) == NULL)
		err("malloc:");
	soa.strs = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds) + pad);

	return soa;
}

void
lexemes_soa_resz(struct lexemes_soa *soa)
{
	static_assert(offsetof(struct lexemes_soa, kinds)
	                  < offsetof(struct lexemes_soa, strs),
	              "KINDS is not the first field before STRS");

	size_t ncap, pad, newsz;
	ptrdiff_t off = (char *)soa->strs - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for overflow
	   becomes pretty trivial */
	if ((soa->cap >> (SIZE_WDTH - 1)) != 0) {
		errno = EOVERFLOW;
		err("lexemes_soa_resz:");
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->strs is properly aligned */
	pad = alignof(*soa->strs)
	    - ncap * sizeof(*soa->kinds) % alignof(*soa->strs);
	if (pad == 8)
		pad = 0;

	newsz = ncap * LEXEMES_SOA_BLKSZ + pad;

	if ((soa->kinds = realloc(soa->kinds, newsz)) == NULL)
		err("realloc:");
	soa->strs = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds) + pad);
	memmove(soa->strs, (char *)soa->kinds + off, soa->len * sizeof(*soa->strs));
	soa->cap = ncap;
}
