#ifndef ORYX_ANALYZER_H
#define ORYX_ANALYZER_H

#include <stdint.h>

#include "lexer.h"
#include "parser.h"

enum {
	TYPE_UNSET = 0,
	TYPE_CHECKING = 1,

	/* Signed integers */
	TYPE_I8,
	TYPE_I16,
	TYPE_I32,
	TYPE_I64,
	TYPE_INT,
	TYPE_INT_UNTYPED,

	/* Unsigned integers */
	TYPE_U8,
	TYPE_U16,
	TYPE_U32,
	TYPE_U64,
	TYPE_UINT,

	/* Floating point numbers */
	TYPE_F32,
	TYPE_F64,

	/* Unicode codepoint */
	TYPE_RUNE,

	/* Function type */
	TYPE_FN,

	_TYPE_LAST_ENT,
};

typedef uint8_t type_kind_t_;
static_assert(_TYPE_LAST_ENT - 1 <= (type_kind_t_)-1,
              "Too many AST tokens to fix in TYPE_KIND_T_");

struct type {
	type_kind_t_ kind;
	uint8_t size  : 7; /* bytes */
	bool issigned : 1;

	/* For functions */
	const struct type *params, *ret;
	idx_t_ paramcnt;
};

void analyzeast(struct ast, struct lexemes);

#endif /* !ORYX_ANALYZER_H */
