#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "strview.h"
#include "types.h"

enum {
	LEXEOF,   /* End of token stream */
	LEXIDENT, /* Identifier */
	LEXNUM,   /* Numeric constant */

	LEXAMP    = '&',
	LEXCOLON  = ':',
	LEXEQ     = '=',
	LEXLANGL  = '<',
	LEXLBRACE = '{',
	LEXLBRKT  = '[',
	LEXLPAR   = '(',
	LEXMINUS  = '-',
	LEXPIPE   = '|',
	LEXPLUS   = '+',
	LEXRANGL  = '>',
	LEXRBRACE = '}',
	LEXRBRKT  = ']',
	LEXRPAR   = ')',
	LEXSEMI   = ';',
	LEXSLASH  = '/',
	LEXSTAR   = '*',
	LEXTILDE  = '~',

	/* We keep these exactly 2 away from each other, because ‘<’ and ‘>’ are 2
	   away from each other in ASCII.  This gives us a simple mapping from some
	   token T to the doubled equivalent by doing T += 193. */
	LEXLANGL_DBL = UINT8_MAX - 2, /* << */
	LEXRANGL_DBL = UINT8_MAX - 0, /* >> */

	_LEX_LAST_ENT,
};

static_assert(_LEX_LAST_ENT - 1 <= UINT8_MAX,
              "Too many lexer tokens to fix in uint8_t");

#define LEXEMES_BLKSZ (1 + sizeof(strview_t))

typedef struct {
	uint8_t *kinds;
	strview_t *strs;
	size_t len, cap;
} lexemes_t;

#define lexemes_free(x) free((x).kinds)

lexemes_t lexstring(const uchar *, size_t)
	__attribute__((nonnull));

#endif /* !ORYX_LEXER_H */
