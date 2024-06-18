#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"
#include "parser.h"

#if DEBUG
#	define AST_DFLT_CAP (8)
#else
#	define AST_DFLT_CAP (2048)
#endif
#define SIZE_WDTH (sizeof(size_t) * CHAR_BIT)

typedef idx_t_ parsefn(struct ast *, struct lexemes) __attribute__((nonnull));
static parsefn parseblk, parsedecl, parseexpr, parseproto, parsestmt, parsetype;

static struct ast mkast(void);
static idx_t_ astalloc(struct ast *) __attribute__((nonnull));
static void astresz(struct ast *)    __attribute__((nonnull));

static size_t toksidx;

struct ast
parsetoks(struct lexemes toks)
{
	struct ast ast = mkast();

	for (;;) {
		(void)parsedecl(&ast, toks);
		if (toks.kinds[toksidx] == LEXEOF)
			break;
	}

	return ast;
}

idx_t_
parseblk(struct ast *ast, struct lexemes toks)
{
	idx_t_ i = astalloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTBLK;
	ast->kids[i].lhs = AST_EMPTY;
	ast->kids[i].rhs = 0;

	if (toks.kinds[toksidx++] != LEXLBRACE)
		err("parser: Expected left brace");

	if (toks.kinds[toksidx] != LEXRBRACE) {
		idx_t_ stmt = parsestmt(ast, toks);
		ast->kids[i].lhs = ast->kids[i].rhs = stmt;
	}

	while (toks.kinds[toksidx] != LEXRBRACE) {
		idx_t_ stmt = parsestmt(ast, toks);
		ast->kids[i].rhs = stmt;
	}

	toksidx++; /* Eat rbrace */
	return i;
}

idx_t_
parsedecl(struct ast *ast, struct lexemes toks)
{
	idx_t_ i = astalloc(ast);
	ast->lexemes[i] = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected identifier");
	if (toks.kinds[toksidx++] != LEXCOLON)
		err("parser: Expected colon");

	idx_t_ lhs = toks.kinds[toksidx] == LEXIDENT ? parsetype(ast, toks)
	                                             : AST_EMPTY;
	ast->kids[i].lhs = lhs;

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

	idx_t_ rhs = parseexpr(ast, toks);
	ast->kids[i].rhs = rhs;
	if (toks.kinds[toksidx++] != LEXSEMI)
		err("parser: Expected semicolon");

	return i;
}

idx_t_
parseexpr(struct ast *ast, struct lexemes toks)
{
	idx_t_ i = astalloc(ast);
	ast->lexemes[i] = toksidx;

	switch (toks.kinds[toksidx]) {
	case LEXNUM:
		toksidx++;
		ast->kinds[i] = ASTNUMLIT;
		break;
	case LEXIDENT:
		toksidx++;
		ast->kinds[i] = ASTIDENT;
		break;
	case LEXLPAR:
		ast->kinds[i] = ASTFN;
		idx_t_ lhs = parseproto(ast, toks);
		idx_t_ rhs = parseblk(ast, toks);
		ast->kids[i].lhs = lhs;
		ast->kids[i].rhs = rhs;
		break;
	default:
		err("parser: Expected expression");
	}

	return i;
}

idx_t_
parseproto(struct ast *ast, struct lexemes toks)
{
	idx_t_ i = astalloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTFNPROTO;
	ast->kids[i].lhs = AST_EMPTY;

	if (toks.kinds[toksidx++] != LEXLPAR)
		err("parser: Expected left parenthesis");
	if (toks.kinds[toksidx++] != LEXRPAR)
		err("parser: Expected right parenthesis");

	idx_t_ rhs = toks.kinds[toksidx] == LEXIDENT ? parsetype(ast, toks)
	                                             : AST_EMPTY;
	ast->kids[i].rhs = rhs;
	return i;
}

idx_t_
parsestmt(struct ast *ast, struct lexemes toks)
{
	idx_t_ i;

	if (toks.kinds[toksidx] != LEXIDENT)
		err("parser: Expected identifier");

	struct strview sv = toks.strs[toksidx];
	if (strncmp("return", sv.p, sv.len) == 0) {
		i = astalloc(ast);
		ast->lexemes[i] = toksidx++;
		ast->kinds[i] = ASTRET;

		idx_t_ rhs = toks.kinds[toksidx] != LEXSEMI ? parseexpr(ast, toks)
		                                            : AST_EMPTY;
		ast->kids[i].rhs = rhs;
		if (toks.kinds[toksidx++] != LEXSEMI)
			err("parser: Expected semicolon");
	} else if (toks.kinds[toksidx + 1] == LEXCOLON)
		i = parsedecl(ast, toks);
	else
		i = parseexpr(ast, toks);

	return i;
}

idx_t_
parsetype(struct ast *ast, struct lexemes toks)
{
	idx_t_ i = astalloc(ast);
	ast->kinds[i] = ASTTYPE;
	ast->lexemes[i] = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected type");

	return i;
}

struct ast
mkast(void)
{
	struct ast soa;

	static_assert(AST_DFLT_CAP * sizeof(*soa.kinds) % alignof(idx_t_) == 0,
	              "Additional padding is required to properly align LEXEMES");
	static_assert(AST_DFLT_CAP * (sizeof(*soa.kinds) + sizeof(*soa.lexemes))
	                      % alignof(struct pair)
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
astresz(struct ast *soa)
{
	size_t ncap, pad1, pad2, newsz;
	ptrdiff_t lexemes_off, kids_off;

	lexemes_off = (char *)soa->lexemes - (char *)soa->kinds;
	kids_off = (char *)soa->kids - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for overflow
	   becomes pretty trivial */
	if ((soa->cap >> (SIZE_WDTH - 1)) != 0) {
		errno = EOVERFLOW;
		err("%s:", __func__);
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->lexemes is properly aligned */
	pad1 = alignof(idx_t_) - ncap * sizeof(ast_kind_t_) % alignof(idx_t_);
	if (pad1 == alignof(idx_t_))
		pad1 = 0;

	/* Ensure that soa->kids is properly aligned */
	pad2 = alignof(struct pair)
	     - (ncap * (sizeof(ast_kind_t_) + sizeof(idx_t_)) + pad1)
	           % alignof(struct pair);
	if (pad2 == alignof(struct pair))
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
astalloc(struct ast *soa)
{
	if (soa->len == soa->cap)
		astresz(soa);
	return soa->len++;
}
