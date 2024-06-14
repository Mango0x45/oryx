#ifndef ORYX_PARSER_H
#define ORYX_PARSER_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lexer.h"
#include "types.h"

enum {
	/* Variable declaration, lhs and rhs may be unused
	   ‘x: lhs = rhs’ */
	ASTDECL,

	/* Constant declaration, lhs and rhs may be unused
	   ‘x: lhs : rhs’ */
	ASTCDECL,

	/* Function prototype
	   ‘(a: b, c: d) rhs’; aux[lhs].fnproto */
	ASTFNPROTO,

	/* Function, lhs is the prototype and rhs is the body block */
	ASTFN,

	/* Braced block, sublist[lhs…rhs] */
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

	_AST_LAST_ENT = 256,
};

typedef uint8_t ast_kind_t_;
static_assert(_AST_LAST_ENT - 1 <= (ast_kind_t_)-1,
              "Too many AST tokens to fix in AST_KIND_T_");

#define AST_EMPTY     ((idx_t_)-1)
#define AST_SOA_BLKSZ (sizeof(ast_kind_t_) + sizeof(idx_t_) * 3)

struct ast {
	ast_kind_t_ *kinds;
	idx_t_ *lexemes;
	struct pair {
		idx_t_ lhs, rhs;
	} *kids;
	size_t len, cap;
};

#define ast_free(x) free((x).kinds)

/* Parse the tokens in TOKS into an abstract syntax tree */
struct ast parsetoks(struct lexemes toks);

#endif /* !ORYX_PARSER_H */
