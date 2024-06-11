#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "parser.h"

/* #define AST_DFLT_CAP (2048) */
#define AST_DFLT_CAP (8)
#define AST_EMPTY    ((size_t)-1)
#define AUX_DFLT_CAP (128)
#define SIZE_WDTH    (sizeof(size_t) * CHAR_BIT)

static size_t parsedecl(struct ast_soa *, struct lexemes_soa);
static size_t parseexpr(struct ast_soa *, struct lexemes_soa);
static size_t parsetype(struct ast_soa *, struct lexemes_soa);

static struct ast_soa mk_ast_soa(void);
static void ast_soa_resz(struct ast_soa *);

static size_t aux_push(struct auxilliary *, union extra);
static size_t ast_soa_push(struct ast_soa *, ast_kind, size_t, size_t, size_t,
                           size_t);

static size_t toksidx;

struct ast_soa
parsetoks(struct lexemes_soa toks)
{
	struct ast_soa ast = mk_ast_soa();

	while (toksidx < toks.len)
		parsedecl(&ast, toks);

	return ast;
}

size_t
parsedecl(struct ast_soa *ast, struct lexemes_soa toks)
{
	bool constdecl;
	size_t lexeme, lhs, rhs;

	lexeme = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("Expected identifier");
	if (toks.kinds[toksidx++] != LEXCOLON)
		err("Expected colon");

	switch (toks.kinds[toksidx]) {
	case LEXCOLON:
	case LEXEQ:
		constdecl = toks.kinds[toksidx++] == LEXCOLON;
		lhs = AST_EMPTY;
		rhs = parseexpr(ast, toks);
		break;
	default:
		lhs = parsetype(ast, toks);
		if (toks.kinds[toksidx] == LEXCOLON || toks.kinds[toksidx] == LEXEQ) {
			constdecl = toks.kinds[toksidx++] == LEXEQ;
			rhs = parseexpr(ast, toks);
		} else {
			constdecl = false;
			rhs = AST_EMPTY;
		}
		break;
	}

	if (toks.kinds[toksidx++] != LEXSEMI)
		err("Expected semicolon");

	size_t extra = aux_push(&ast->aux, (union extra){.constdecl = constdecl});
	return ast_soa_push(ast, PRSDECL, lexeme, lhs, rhs, extra);
}

size_t
parseexpr(struct ast_soa *ast, struct lexemes_soa toks)
{
	ast_kind kind;
	size_t lexeme, lhs, rhs, extra;

	lexeme = lhs = rhs = extra = AST_EMPTY;

	switch (toks.kinds[toksidx]) {
	case LEXNUM:
		kind = PRSNUMERIC;
		lexeme = toksidx++;
		break;
	default:
		err("Expected expression");
	}

	return ast_soa_push(ast, kind, lexeme, lhs, rhs, extra);
}

size_t
parsetype(struct ast_soa *ast, struct lexemes_soa toks)
{
	size_t lexeme;

	switch (toks.kinds[toksidx]) {
	case LEXIDENT:
		lexeme = toksidx++;
		break;
	default:
		err("Expected type");
	}

	return ast_soa_push(ast, PRSTYPE, lexeme, AST_EMPTY, AST_EMPTY, AST_EMPTY);
}

struct ast_soa
mk_ast_soa(void)
{
	struct ast_soa soa;

	static_assert(AST_DFLT_CAP * sizeof(*soa.kinds) % alignof(*soa.lexemes)
	                  == 0,
	              "Additional padding is required to properly align LEXEMES");
	static_assert(AST_DFLT_CAP * (sizeof(*soa.kinds) + sizeof(*soa.lexemes))
	                      % alignof(*soa.kids)
	                  == 0,
	              "Additional padding is required to properly align KIDS");
	static_assert(AST_DFLT_CAP
	                      * (sizeof(*soa.kinds) + sizeof(*soa.lexemes)
	                         + sizeof(*soa.kids))
	                      % alignof(*soa.extra)
	                  == 0,
	              "Additional padding is required to properly align EXTRA");

	soa.len = 0;
	soa.cap = AST_DFLT_CAP;
	soa.aux.len = 0;
	soa.aux.cap = AUX_DFLT_CAP;

	if ((soa.kinds = malloc(soa.cap * AST_SOA_BLKSZ)) == NULL)
		err("malloc:");
	soa.lexemes = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds));
	soa.kids = (void *)((char *)soa.lexemes + soa.cap * sizeof(*soa.lexemes));
	soa.extra = (void *)((char *)soa.kids + soa.cap * sizeof(*soa.kids));

	if ((soa.aux.buf = malloc(soa.aux.cap * sizeof(*soa.aux.buf))) == NULL)
		err("malloc:");

	return soa;
}

void
ast_soa_resz(struct ast_soa *soa)
{
	size_t ncap, pad1, pad2, pad3, newsz;
	ptrdiff_t lexemes_off, kids_off, extra_off;

	lexemes_off = (char *)soa->lexemes - (char *)soa->kinds;
	kids_off = (char *)soa->kids - (char *)soa->kinds;
	extra_off = (char *)soa->extra - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for overflow
	   becomes pretty trivial */
	if ((soa->cap >> (SIZE_WDTH - 1)) != 0) {
		errno = EOVERFLOW;
		err("ast_soa_resz:");
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->lexemes is properly aligned */
	pad1 = alignof(*soa->lexemes)
	     - ncap * sizeof(*soa->kinds) % alignof(*soa->lexemes);
	if (pad1 == alignof(*soa->lexemes))
		pad1 = 0;

	/* Ensure that soa->kids is properly aligned */
	pad2 = alignof(*soa->kids)
	     - (ncap * (sizeof(*soa->kinds) + sizeof(*soa->lexemes)) + pad1)
	           % alignof(*soa->kids);
	if (pad2 != alignof(*soa->kids))
		pad2 = 0;

	/* Ensure that soa->extra is properly aligned */
	pad3 = alignof(*soa->extra)
	     - (ncap
	            * (sizeof(*soa->kinds) + sizeof(*soa->lexemes)
	               + sizeof(*soa->kids))
	        + pad1 + pad2)
	           % alignof(*soa->extra);
	if (pad3 != alignof(*soa->extra))
		pad3 = 0;

	newsz = ncap * AST_SOA_BLKSZ + pad1 + pad2 + pad3;
	if ((soa->kinds = realloc(soa->kinds, newsz)) == NULL)
		err("realloc:");

	soa->lexemes = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds)
	                        + pad1);
	soa->kids = (void *)((char *)soa->lexemes + ncap * sizeof(*soa->lexemes)
	                     + pad2);
	soa->extra = (void *)((char *)soa->kids + ncap * sizeof(*soa->kids) + pad3);

	memmove(soa->extra, (char *)soa->kinds + extra_off,
	        soa->len * sizeof(*soa->extra));
	memmove(soa->kids, (char *)soa->kinds + kids_off,
	        soa->len * sizeof(*soa->kids));
	memmove(soa->lexemes, (char *)soa->kinds + lexemes_off,
	        soa->len * sizeof(*soa->lexemes));

	soa->cap = ncap;
}

size_t
aux_push(struct auxilliary *aux, union extra e)
{
	if (aux->len == aux->cap) {
		size_t ncap = aux->cap * 2;
		if ((aux->buf = realloc(aux->buf, ncap)) == NULL)
			err("realloc:");
		aux->cap = ncap;
	}
	aux->buf[aux->len] = e;
	return aux->len++;
}

size_t
ast_soa_push(struct ast_soa *soa, ast_kind kind, size_t lexeme, size_t lhs,
             size_t rhs, size_t extra)
{
	if (soa->len == soa->cap)
		ast_soa_resz(soa);

	soa->kinds[soa->len] = kind;
	soa->lexemes[soa->len] = lexeme;
	soa->kids[soa->len].lhs = lhs;
	soa->kids[soa->len].rhs = rhs;
	soa->extra[soa->len] = extra;
	return soa->len++;
}
