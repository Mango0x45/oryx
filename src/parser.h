#ifndef ORYX_PARSER_H
#define ORYX_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lexer.h"

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
};

typedef uint8_t ast_kind;

#define AST_SOA_BLKSZ (sizeof(ast_kind) + sizeof(size_t) * 4)

struct ast_soa {
	ast_kind *kinds;
	size_t *lexemes;
	struct {
		size_t lhs, rhs;
	} *kids;
	size_t len, cap;
};

#define ast_free(x) free((x).kinds)

/* Parse the tokens in TOKS into an abstract syntax tree */
struct ast_soa parsetoks(struct lexemes_soa toks);

#endif /* !ORYX_PARSER_H */
