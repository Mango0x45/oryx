#ifndef ORYX_LEXER_H
#define ORYX_LEXER_H

#include <stddef.h>
#include <stdint.h>

enum {
	LEXIDENT,
};

typedef uint8_t lexeme_kind;

struct lexeme {
	lexeme_kind kind;
	const char *p;
	size_t len;
};

struct lexeme *lexstring(const char *, size_t, size_t *);

#endif /* !ORYX_LEXER_H */
