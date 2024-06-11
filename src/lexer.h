#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

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
};

typedef uint8_t lexeme_kind;

#define LEXEMES_SOA_BLKSZ (sizeof(lexeme_kind) + sizeof(struct strview))

struct lexemes_soa {
	lexeme_kind *kinds;
	struct strview *strs;
	size_t len, cap;
};

#define lexemes_free(x) free((x).kinds)

struct lexemes_soa lexstring(const uchar *, size_t);

#endif /* !ORYX_LEXER_H */
