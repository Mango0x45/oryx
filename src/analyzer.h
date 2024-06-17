#ifndef ORYX_ANALYZER_H
#define ORYX_ANALYZER_H

#include <stdint.h>

#include "lexer.h"
#include "parser.h"

enum {
	TYPE_UNSET = 0,
	TYPE_CHECKING = 1,

	TYPE_I8,
	TYPE_I16,
	TYPE_I32,
	TYPE_I64,
	TYPE_INT,
	TYPE_INT_UNTYPED,

	TYPE_U8,
	TYPE_U16,
	TYPE_U32,
	TYPE_U64,
	TYPE_UINT,

	TYPE_RUNE,
};

typedef uint8_t type_kind_t_;

struct type {
	type_kind_t_ kind;
	uint8_t size  : 7; /* bytes */
	bool issigned : 1;
};

void analyzeast(struct ast, struct lexemes);

#endif /* !ORYX_ANALYZER_H */
