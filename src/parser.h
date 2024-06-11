#ifndef ORYX_PARSER_H
#define ORYX_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lexer.h"

enum {
	PRSDECL,         /* Declaration */
	PRSNUMERIC,      /* Numeric constant */
	PRSTYPE,         /* Type */

	PRSBINADD = '+', /* Addition */
};

typedef uint8_t ast_kind;

#define AST_SOA_BLKSZ (sizeof(ast_kind) + sizeof(size_t) * 4)

/*
 * AST Notes:
 *
 * Declarations have .lhs set to the type of the symbol being declared and .rhs
 * set to the value of the declaration if it is an assignment declaration.  They
 * also have aux.constdecl set to true if it’s a declaration of a constant.
 */

struct auxilliary {
	union extra {
		bool constdecl;
	} *buf;
	size_t len, cap;
};

struct ast_soa {
	ast_kind *kinds;
	size_t *lexemes;
	struct {
		size_t lhs, rhs;
	} *kids;
	size_t *extra;
	size_t len, cap;

	struct auxilliary aux;
};

#define ast_free(x) do { free((x).kinds); free((x).aux.buf); } while (0)

/* Parse the tokens in TOKS into an abstract syntax tree */
struct ast_soa parsetoks(struct lexemes_soa toks);

#endif /* !ORYX_PARSER_H */
