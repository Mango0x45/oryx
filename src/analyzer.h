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

static_assert(_TYPE_LAST_ENT - 1 <= UINT8_MAX,
              "Too many AST tokens to fix in uint8_t");

typedef struct symtab symtab;

typedef struct {
	idx_t up, i;
	symtab *map;
} scope_t;

/* A variable type */
typedef struct type {
	uint8_t kind;

	union {
		struct {
			uint8_t size; /* number of bytes */
			bool issigned;
			bool isfloat;
		};
		struct  {
			const struct type *params, *ret;
			idx_t paramcnt;
		};
	};
} type_t;

void analyzeprog(ast_t, aux_t, lexemes_t, arena_t *, type_t **, scope_t **,
                 mpq_t **)
	__attribute__((nonnull));

#endif /* !ORYX_ANALYZER_H */
