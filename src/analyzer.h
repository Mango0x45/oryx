#ifndef ORYX_ANALYZER_H
#define ORYX_ANALYZER_H

#include <stdint.h>

#include "lexer.h"
#include "parser.h"

enum {
	TYPE_UNSET = 0,
	TYPE_CHECKING = 1,
	TYPE_NUM,
	TYPE_FN,
	_TYPE_LAST_ENT,
};

typedef uint8_t type_kind_t_;
static_assert(_TYPE_LAST_ENT - 1 <= (type_kind_t_)-1,
              "Too many AST tokens to fix in TYPE_KIND_T_");

struct type {
	type_kind_t_ kind;
	uint8_t size  : 6; /* bytes */
	bool issigned : 1;
	bool isfloat  : 1;

	/* For functions */
	const struct type *params, *ret;
	idx_t_ paramcnt;
};

struct type *analyzeast(struct ast, struct lexemes)
	__attribute__((returns_nonnull));

#endif /* !ORYX_ANALYZER_H */
