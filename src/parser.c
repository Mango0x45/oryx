#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "errors.h"
#include "parser.h"

/* #define AST_DFLT_CAP (2048) */
#define AST_DFLT_CAP (8)
#define SIZE_WDTH    (sizeof(size_t) * CHAR_BIT)

typedef idx_t_ parsefn(struct ast_soa *, struct lexemes_soa);
static parsefn parseblk,
               parsedecl,
               parseexpr,
               parseproto,
               parsestmt,
               parsetype;

static idx_t_ ast_alloc(struct ast_soa *);
static struct ast_soa mk_ast_soa(void);
static void ast_soa_resz(struct ast_soa *);

static size_t toksidx;

struct ast_soa
parsetoks(struct lexemes_soa toks)
{
	struct ast_soa ast = mk_ast_soa();

	for (;;) {
		parsedecl(&ast, toks);
		if (toks.kinds[toksidx] == LEXEOF)
			break;
	}

	return ast;
}

idx_t_
parseblk(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i = ast_alloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTBLK;
	ast->kids[i].lhs = ast->kids[i].rhs = AST_EMPTY;

	if (toks.kinds[toksidx++] != LEXLBRACE)
		err("parser: Expected left brace");

	while (toks.kinds[toksidx] != LEXRBRACE) {
		ast->kids[i].rhs = parsestmt(ast, toks);
		if (ast->kids[i].lhs == AST_EMPTY)
			ast->kids[i].lhs = ast->kids[i].rhs;
	}

	toksidx++; /* Eat rbrace */
	return i;
}

idx_t_
parsedecl(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i = ast_alloc(ast);
	ast->lexemes[i] = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected identifier");
	if (toks.kinds[toksidx++] != LEXCOLON)
		err("parser: Expected colon");

	ast->kids[i].lhs = toks.kinds[toksidx] == LEXIDENT
	                 ? parsetype(ast, toks)
	                 : AST_EMPTY;

	switch (toks.kinds[toksidx++]) {
	case LEXSEMI:
		if (ast->kids[i].lhs == AST_EMPTY)
			err("parser: No type provided in non-assigning declaration");
		ast->kinds[i] = ASTDECL;
		ast->kids[i].rhs = AST_EMPTY;
		return i;
	case LEXCOLON:
		ast->kinds[i] = ASTCDECL;
		break;
	case LEXEQ:
		ast->kinds[i] = ASTDECL;
		break;
	default:
		err("parser: Expected semicolon or equals");
	}

	ast->kids[i].rhs = parseexpr(ast, toks);
	if (toks.kinds[toksidx++] != LEXSEMI)
		err("parser: Expected semicolon");

	return i;
}

idx_t_
parseexpr(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i = ast_alloc(ast);
	ast->lexemes[i] = toksidx;

	switch (toks.kinds[toksidx]) {
	case LEXNUM:
		toksidx++;
		ast->kinds[i] = ASTNUMLIT;
		break;
	case LEXLPAR:
		ast->kinds[i] = ASTFN;
		ast->kids[i].lhs = parseproto(ast, toks);
		ast->kids[i].rhs = parseblk(ast, toks);
		break;
	default:
		err("parser: Expected expression");
	}

	return i;
}

idx_t_
parseproto(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i = ast_alloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTFNPROTO;
	ast->kids[i].lhs = AST_EMPTY;

	if (toks.kinds[toksidx++] != LEXLPAR)
		err("parser: Expected left parenthesis");
	if (toks.kinds[toksidx++] != LEXRPAR)
		err("parser: Expected right parenthesis");

	ast->kids[i].rhs = toks.kinds[toksidx] == LEXIDENT
	                 ? parsetype(ast, toks)
	                 : AST_EMPTY;
	return i;
}

idx_t_
parsestmt(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i;

	if (toks.kinds[toksidx] != LEXIDENT)
		err("parser: Expected identifier");

	struct strview sv = toks.strs[toksidx];
	if (strncmp("return", sv.p, sv.len) == 0) {
		i = ast_alloc(ast);
		ast->lexemes[i] = toksidx++;
		ast->kinds[i] = ASTRET;
		if (toks.kinds[toksidx] != LEXSEMI)
			ast->kids[i].rhs = parseexpr(ast, toks);
		else
			ast->kids[i].rhs = AST_EMPTY;
		if (toks.kinds[toksidx++] != LEXSEMI)
			err("parser: Expected semicolon");
	} else if (toks.kinds[toksidx + 1] == LEXCOLON)
		i = parsedecl(ast, toks);
	else
		i = parseexpr(ast, toks);

	return i;
}

idx_t_
parsetype(struct ast_soa *ast, struct lexemes_soa toks)
{
	idx_t_ i = ast_alloc(ast);
	ast->kinds[i] = ASTTYPE;
	ast->lexemes[i] = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected type");

	return i;
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

	soa.len = 0;
	soa.cap = AST_DFLT_CAP;

	soa.kinds = bufalloc(NULL, soa.cap, AST_SOA_BLKSZ);
	soa.lexemes = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds));
	soa.kids = (void *)((char *)soa.lexemes + soa.cap * sizeof(*soa.lexemes));

	return soa;
}

void
ast_soa_resz(struct ast_soa *soa)
{
	size_t ncap, pad1, pad2, newsz;
	ptrdiff_t lexemes_off, kids_off;

	lexemes_off = (char *)soa->lexemes - (char *)soa->kinds;
	kids_off = (char *)soa->kids - (char *)soa->kinds;

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

	newsz = ncap * AST_SOA_BLKSZ + pad1 + pad2;
	soa->kinds = bufalloc(soa->kinds, newsz, 1);
	soa->lexemes = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds)
	                        + pad1);
	soa->kids = (void *)((char *)soa->lexemes + ncap * sizeof(*soa->lexemes)
	                     + pad2);

	memmove(soa->kids, (char *)soa->kinds + kids_off,
	        soa->len * sizeof(*soa->kids));
	memmove(soa->lexemes, (char *)soa->kinds + lexemes_off,
	        soa->len * sizeof(*soa->lexemes));

	soa->cap = ncap;
}

idx_t_
ast_alloc(struct ast_soa *soa)
{
	if (soa->len == soa->cap)
		ast_soa_resz(soa);
	return soa->len++;
}
