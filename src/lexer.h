#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "strview.h"
#include "types.h"

enum {
	LEXEOF,   /* End of token stream */
	LEXIDENT, /* Identifier */
	LEXNUM,   /* Numeric constant */
	LEXELIP,  /* Ellipsis */

	/* NOTE: Make sure that the enumerations above this comment don’t
	   conflict with the following explicitly assigned enumerations! */

	LEXAMP    = '&',
	LEXCOLON  = ':',
	LEXEQ     = '=',
	LEXLANGL  = '<',
	LEXLBRACE = '{',
	LEXLBRKT  = '[',
	LEXLPAR   = '(',
	LEXMINUS  = '-',
	LEXPERC   = '%',
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

	LEXBANGEQ = UINT8_MAX - 3, /* Not equals */

	/* This gives us a simple mapping from some token T to the doubled
	   equivalent by doing T += 193. */
	LEXLANGL_DBL = UINT8_MAX - 2, /* << */
	LEXEQ_DBL    = UINT8_MAX - 1, /* == */
	LEXRANGL_DBL = UINT8_MAX - 0, /* >> */
};

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
