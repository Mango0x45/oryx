#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

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

typedef uint8_t lexeme_kind_t_;
static_assert(_LEX_LAST_ENT - 1 <= (lexeme_kind_t_)-1,
              "Too many lexer tokens to fix in LEXEME_KIND_T_");

#define LEXEMES_BLKSZ (sizeof(lexeme_kind_t_) + sizeof(struct strview))

struct lexemes {
	lexeme_kind_t_ *kinds;
	struct strview *strs;
	size_t len, cap;
};

#define lexemes_free(x) free((x).kinds)

struct lexemes lexstring(const uchar *, size_t);

#endif /* !ORYX_LEXER_H */
