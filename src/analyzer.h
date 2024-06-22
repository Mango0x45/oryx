#ifndef ORYX_ANALYZER_H
#define ORYX_ANALYZER_H

#include <stdint.h>

#include <gmp.h>

#include "alloc.h"
#include "lexer.h"
#include "parser.h"
#include "types.h"

/* The different base types */
enum {
	/* No type exists (or hasn’t yet been typechecked) */
	TYPE_UNSET,

	/* Currently in the process of being typechecked.  Useful for
	   detecting cyclic definitions. */
	TYPE_CHECKING,

	/* A numeric type */
	TYPE_NUM,

	/* A function type */
	TYPE_FN,

	_TYPE_LAST_ENT,
};

typedef uint8_t type_kind_t_;
static_assert(_TYPE_LAST_ENT - 1 <= (type_kind_t_)-1,
              "Too many AST tokens to fix in TYPE_KIND_T_");

typedef struct symtab symtab;

struct scope {
	idx_t_ up, i;
	symtab *map;
};

/* A variable type */
struct type {
	type_kind_t_ kind;

	union {
		struct {
			uint8_t size;
			bool issigned;
			bool isfloat;
		};
		struct  {
			const struct type *params, *ret;
			idx_t_ paramcnt;
		};
	};
};

void analyzeprog(struct ast, struct aux, struct lexemes, arena *,
                 struct type **, struct scope **, mpq_t **)
	__attribute__((nonnull));

#endif /* !ORYX_ANALYZER_H */
