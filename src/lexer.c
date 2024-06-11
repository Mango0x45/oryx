#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "errors.h"
#include "lexer.h"
#include "unicode.h"

#define LEXEMES_DFLT_CAP (2048)
#define SIZE_WDTH        (sizeof(size_t) * CHAR_BIT)

static bool skip_comment(const uchar **, const uchar *);

static struct lexemes_soa mk_lexemes_soa(void);
static void lexemes_soa_resz(struct lexemes_soa *);

static const bool is_numeric_lookup[UCHAR_MAX + 1] = {
	['0'] = true, ['1'] = true, ['2'] = true,  ['3'] = true,
	['4'] = true, ['5'] = true, ['6'] = true,  ['7'] = true,
	['8'] = true, ['9'] = true, ['\''] = true,
};

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
		case '&': case '(': case ')': case '*': case '+':
		case '-': case ':': case ';': case '=': case '[':
		case ']': case '{': case '|': case '}': case '~':
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

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			data.kinds[data.len] = LEXNUM;
			data.strs[data.len].p = spnbeg;

			while (likely(code < end) && is_numeric_lookup[code[0]]) {
				if (unlikely(code[0] == '\'' && code[-1] == '\'')) {
					err("Adjacent numeric separators at byte %td",
					    code - start);
				}
				code++;
			}
			if (unlikely(code < end && code[-1] == '\'')) {
				err("Numeric literal ends with numeric separator at byte %td",
				    code - start);
			}

			data.strs[data.len++].len = code - spnbeg;
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

	if (unlikely(data.len == data.cap))
		lexemes_soa_resz(&data);
	data.kinds[data.len++] = LEXEOF;
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
	struct lexemes_soa soa;

	static_assert(offsetof(struct lexemes_soa, kinds)
	                  < offsetof(struct lexemes_soa, strs),
	              "KINDS is not the first field before STRS");
	static_assert(LEXEMES_DFLT_CAP * sizeof(*soa.kinds) % alignof(*soa.strs)
	                  == 0,
	              "Additional padding is required to properly align STRS");

	soa.len = 0;
	soa.cap = LEXEMES_DFLT_CAP;
	soa.kinds = bufalloc(NULL, soa.cap, LEXEMES_SOA_BLKSZ);
	soa.strs = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds));

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
	if (pad == alignof(*soa->strs))
		pad = 0;

	newsz = ncap * LEXEMES_SOA_BLKSZ + pad;

	soa->kinds = bufalloc(soa->kinds, newsz, 1);
	soa->strs = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds) + pad);
	memmove(soa->strs, (char *)soa->kinds + off, soa->len * sizeof(*soa->strs));
	soa->cap = ncap;
}
