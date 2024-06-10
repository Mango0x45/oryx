#ifndef ORYX_PARSER_H
#define ORYX_PARSER_H

#include <stddef.h>
#include <stdint.h>

#include "lexer.h"

enum {
	PRSBINADD = '+',
	PRSBINSUB = '-',
};

typedef uint8_t ast_kind;

#define AST_SOA_BLKSZ (sizeof(ast_kind) + sizeof(size_t) * 3)

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
