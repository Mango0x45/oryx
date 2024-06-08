#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

#include <stddef.h>
#include <stdint.h>

enum {
	LEXIDENT,

	LEXCOLON = ':',
	LEXLBRACE = '{',
	LEXLPAR = '(',
	LEXRBRACE = '}',
	LEXRPAR = ')',
	LEXSEMI = ';',

	LEXAMP = '&',
	LEXEQ = '=',
	LEXLANGL = '<',
	LEXMINUS = '-',
	LEXPIPE = '|',
	LEXPLUS = '+',
	LEXRANGL = '>',
	LEXSLASH = '/',
	LEXSTAR = '*',
	LEXTILDE = '~',

	/* We keep these exactly 2 away from each other, because ‘<’ and ‘>’ are 2
	   away from each other in ASCII.  This gives us a simple mapping from some
	   token T to the doubled equivalent by doing T += 193. */
	LEXLANGL_DBL = UINT8_MAX - 2, /* << */
	LEXRANGL_DBL = UINT8_MAX - 0, /* >> */
};

typedef uint8_t lexeme_kind;

struct lexeme {
	lexeme_kind kind;
	const char *p;
	size_t len;
};

struct lexeme *lexstring(const unsigned char *, size_t, size_t *);

#endif /* !ORYX_LEXER_H */
