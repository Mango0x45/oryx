#ifndef ORYX_ANALYZER_H
#define ORYX_ANALYZER_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <gmp.h>

#include "alloc.h"
#include "bitset.h"
#include "lexer.h"
#include "parser.h"
#include "tables.h"
#include "types.h"

/* The different base types */
enum {
	/* Currently in the process of being typechecked.  Useful for
	   detecting cyclic definitions. */
	TYPE_CHECKING,

	TYPE_BOOL,
	TYPE_NUM,
	TYPE_FN,

	_TYPE_LAST_ENT,
};

static_assert(_TYPE_LAST_ENT - 1 <= UINT8_MAX,
              "Too many AST tokens to fix in uint8_t");

typedef struct {
	idx_t up, i;
	symtab_t *map;
} scope_t;

typedef union {
	char data[sizeof(mpq_t)];
	mpq_t q;
	struct { bool b, set; };
} fold_t;

type_t **analyzeprog(ast_t, aux_t, lexemes_t, arena_t *, scope_t **, fold_t **,
                     bitset_t **)
	__attribute__((returns_nonnull, nonnull));

#endif /* !ORYX_ANALYZER_H */
