#ifndef ORYX_PARSER_H
#define ORYX_PARSER_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lexer.h"
#include "types.h"

enum {
	/* The first four AST tokens are declarations.  A declaration is any
	   token T for which ‘T <= _AST_DECLS_END’ holds.  Declarations can
	   also be made public by using the ‘pub’ keyword, and you can tell
	   if a declaration is public by if the LSB is set. */

	/* Variable declaration, lhs and rhs may be unused
	   ‘x := rhs’; aux[lhs].decl */
	ASTDECL,

	/* Constant declaration, lhs may be unused
	   ‘x :: rhs’; aux[lhs].decl */
	ASTCDECL,

	_AST_DECLS_END = ASTCDECL,

	/* Function prototype
	   ‘(a: b, c: d) rhs’; aux[lhs].fnproto */
	ASTFNPROTO,

	/* Function
	   ‘(…)@lhs {…}@rhs’ */
	ASTFN,

	/* Braced block, an empty block has lhs = AST_EMPTY and rhs = 0
	   { stmt@lhs; …; stmt@rhs; } */
	ASTBLK,

	/* Identifier literal */
	ASTIDENT,

	/* Numeric literal */
	ASTNUMLIT,

	/* Typename */
	ASTTYPE,

	/* Return statement, rhs may be unused
	   ‘return rhs’ */
	ASTRET,

	/* Binary add
	   ‘lhs + rhs’ */
	ASTBINADD = '+',

	/* Binary sub
	   ‘lhs - rhs’ */
	ASTBINSUB = '-',

	_AST_LAST_ENT,
};

static_assert(_AST_LAST_ENT - 1 <= UINT8_MAX,
              "Too many AST tokens to fix in uint8_t");

#define AST_EMPTY     ((idx_t)-1)
#define AST_SOA_BLKSZ (1 + sizeof(idx_t) + sizeof(pair_t))

typedef struct {
	union {
		struct {
			idx_t type;
			bool ispub;
			bool isstatic;
		} decl;
	} *buf;
	size_t len, cap;
} aux_t;

typedef struct {
	idx_t lhs, rhs;
} pair_t;

typedef struct {
	uint8_t *kinds;
	idx_t *lexemes;
	pair_t *kids;
	size_t len, cap;
} ast_t;

#define ast_free(x) free((x).kinds)
#define aux_free(x) free((x).buf)

/* Parse the tokens in TOKS into an abstract syntax tree, and store
   auxilliary information in AUX */
ast_t parsetoks(lexemes_t toks, aux_t *aux)
	__attribute__((nonnull));

/* Starting from the node at indent I in AST, return the index of the next node
   in AST that is of the same nest-depth as I */
idx_t fwdnode(ast_t ast, idx_t i);

#endif /* !ORYX_PARSER_H */
