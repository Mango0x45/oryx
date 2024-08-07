#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "strview.h"
#include "unicode.h"

#if DEBUG
#	define LEXEMES_DFLT_CAP (8)
#else
#	define LEXEMES_DFLT_CAP (2048)
#endif
#define SIZE_WDTH (sizeof(size_t) * CHAR_BIT)

static lexemes_t mklexemes(void);

/* Resize TOKS to the next power-of-2 capacity */
static void lexemesresz(lexemes_t *toks)
	__attribute__((nonnull));

/* Advance PTR (which points to the start of a comment) to the end of the
   comment, or END.  Returns true if the comment was well-formed and
   false if the comment was unterminated.  Handles nested comments. */
bool skpcmnt(const uchar **ptr, const uchar *end)
	__attribute__((nonnull));

static const bool is_numeric_lookup[UCHAR_MAX + 1] = {
	['0'] = true, ['1'] = true, ['2'] = true,  ['3'] = true,
	['4'] = true, ['5'] = true, ['6'] = true,  ['7'] = true,
	['8'] = true, ['9'] = true, ['\''] = true, ['.'] = true,
};

lexemes_t
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

	lexemes_t data = mklexemes();

	const uchar *start = code, *end = start + codesz;
	while (likely(code < end)) {
		const uchar *spnbeg = code, *spnend;
		rune ch = utf8_decode(&code);

		switch (ch) {
		/* Whitespace */
		case '\t': case '\n': case '\v':
		case '\f': case '\r': case ' ':
		case RUNE_C(0x0085): /* NEXT LINE */
		case RUNE_C(0x200E): /* LEFT-TO-RIGHT MARK */
		case RUNE_C(0x200F): /* RIGHT-TO-LEFT MARK */
		case RUNE_C(0x2028): /* LINE SEPARATOR */
		case RUNE_C(0x2029): /* PARAGRAPH SEPARATOR */
			break;

		/* Single-byte literals */
		case '%': case '&': case '(': case ')': case '*':
		case '+': case '-': case ':': case ';': case '[':
		case ']': case '{': case '|': case '}': case '~':
			data.kinds[data.len++] = ch;
			break;

		case RUNE_C(0x2026): /* HORIZONTAL ELLIPSIS */
			data.kinds[data.len++] = LEXELIP;
			break;

		/* Single- or double-byte literals */
		case '/':
			if (likely(code < end) && code[0] == '*') {
				if (!skpcmnt(&code, end))
					err("Unterminated comment at byte %td", code - start);
				continue;
			}

			data.kinds[data.len++] = ch;
			break;
		case '!':
			if (unlikely(code == end || code[0] != '='))
				goto fallback;
			code++;
			data.kinds[data.len++] = LEXBANGEQ;
			break;
		case '<': case '=': case '>':
			data.kinds[data.len++] = ch;

			/* See the comment in lexer.h for where 193 comes from */
			if (likely(code < end) && code[0] == ch) {
				code++;
				data.kinds[data.len - 1] += 193;
			}
			break;

		case '.':
			if (unlikely(end - code < 2) || code[0] != '.' || code[1] != '.') {
				if (likely(end - code) >= 1 && isdigit(code[0]))
					goto number;
				goto fallback;
			}
			code += 2;
			data.kinds[data.len++] = LEXELIP;
			break;

		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
number:
		{
			bool saw_dot = false;
			data.kinds[data.len] = LEXNUM;
			data.strs[data.len].p = spnbeg;

			while (likely(code < end) && is_numeric_lookup[code[0]]) {
				if (unlikely(code[0] == '.')) {
					if (saw_dot)
						err("lexer: Decimal separator given multiple times in numeric literal");
					saw_dot = true;
				}
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
		}

		default:
fallback:
			if (unlikely(!rune_is_xids(ch)))
				err("lexer: Unexpected rune U+%04" PRIXRUNE, ch);

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
			lexemesresz(&data);
	}

	if (unlikely(data.len == data.cap))
		lexemesresz(&data);
	data.kinds[data.len++] = LEXEOF;
	return data;
}

lexemes_t
mklexemes(void)
{
	lexemes_t soa;

	static_assert(offsetof(lexemes_t, kinds) < offsetof(lexemes_t, strs),
	              "KINDS is not the first field before STRS");
	static_assert(LEXEMES_DFLT_CAP * sizeof(*soa.kinds) % alignof(strview_t)
	                  == 0,
	              "Additional padding is required to properly align STRS");

	soa.len = 0;
	soa.cap = LEXEMES_DFLT_CAP;
	soa.kinds = bufalloc(NULL, soa.cap, LEXEMES_BLKSZ);
	soa.strs = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds));

	return soa;
}

void
lexemesresz(lexemes_t *soa)
{
	static_assert(offsetof(lexemes_t, kinds) < offsetof(lexemes_t, strs),
	              "KINDS is not the first field before STRS");

	size_t ncap, pad, newsz;
	ptrdiff_t off = (char *)soa->strs - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for
	   overflow becomes pretty trivial */
	if (unlikely((soa->cap >> (SIZE_WDTH - 1)) != 0)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->strs is properly aligned */
	pad = alignof(strview_t)
	    - ncap * sizeof(*soa->kinds) % alignof(strview_t);
	if (pad == alignof(strview_t))
		pad = 0;

	newsz = ncap * LEXEMES_BLKSZ + pad;

	soa->kinds = bufalloc(soa->kinds, newsz, 1);
	soa->strs = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds) + pad);
	memmove(soa->strs, (char *)soa->kinds + off, soa->len * sizeof(*soa->strs));
	soa->cap = ncap;
}
