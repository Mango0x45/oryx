#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "strview.h"

#if DEBUG
#	define AST_DFLT_CAP (8)
#	define AUX_DFLT_CAP (8)
#else
#	define AST_DFLT_CAP (2048)
#	define AUX_DFLT_CAP (128)
#endif
#define SIZE_WDTH (sizeof(size_t) * CHAR_BIT)

typedef idx_t parsefn(ast_t *, aux_t *, lexemes_t)
	__attribute__((nonnull));
static parsefn parseblk, parsefunc, parseproto, parsestmt, parsetype;
static idx_t parsedecl(ast_t *, aux_t *, lexemes_t, bool)
	__attribute__((nonnull));
static idx_t parseexpr(ast_t *, lexemes_t, int)
	__attribute__((nonnull));
static idx_t parseexprinc(ast_t *, lexemes_t, idx_t, int)
	__attribute__((nonnull));
static idx_t parseexpratom(ast_t *, lexemes_t)
	__attribute__((nonnull));
static bool isfunc(lexemes_t);

static ast_t mkast(void);

/* Return a new index in AST where a node can be stored.  This function
   automatically resizes AST if it runs out of capacity. */
static idx_t astalloc(ast_t *ast)
	__attribute__((nonnull));

/* Resize AST to the next power-of-2 capacity */
static void astresz(ast_t *ast)
	__attribute__((nonnull));

/* TODO: Make thread-local? */
static size_t toksidx;

idx_t
fwdnode(ast_t ast, idx_t i)
{
	while (likely(i < ast.len)) {
		switch (ast.kinds[i]) {
		case ASTBLK:
			i = ast.kids[i].lhs == AST_EMPTY ? i + 1 : ast.kids[i].rhs;
			break;
		case ASTDECL:
			i = ast.kids[i].rhs == AST_EMPTY ? i + 1 : ast.kids[i].rhs;
			break;
		case ASTRET:
			if (ast.kids[i].rhs == AST_EMPTY)
				return i + 1;
			i = ast.kids[i].rhs;
			break;
		case ASTASIGN:
		case ASTBINADD:
		case ASTBINAND:
		case ASTBINDIV:
		case ASTBINEQ:
		case ASTBINIOR:
		case ASTBINMOD:
		case ASTBINMUL:
		case ASTBINNEQ:
		case ASTBINSHL:
		case ASTBINSHR:
		case ASTBINSUB:
		case ASTBINXOR:
		case ASTCDECL:
		case ASTFN:
		case ASTUNCMPL:
		case ASTUNNEG:
		case ASTUNPLUS:
			i = ast.kids[i].rhs;
			break;
		case ASTCALLSTMT:
			i = ast.kids[i + 1].lhs;
			break;
		case ASTFUNCALL:
			i = ast.kids[i].lhs;
			break;
		case ASTIDENT:
		case ASTNUMLIT:
		case ASTTYPE:
			return i + 1;
		case ASTFNPROTO:
		default:
			__builtin_unreachable();
		}
	}

	return i;
}

ast_t
parsetoks(lexemes_t toks, aux_t *aux)
{
	ast_t ast = mkast();
	aux->len = 0;
	aux->cap = AUX_DFLT_CAP;
	aux->buf = bufalloc(NULL, aux->cap, sizeof(*aux->buf));

	for (;;) {
		(void)parsedecl(&ast, aux, toks, true);
		if (toks.kinds[toksidx] == LEXEOF)
			break;
	}

	return ast;
}

idx_t
parseblk(ast_t *ast, aux_t *aux, lexemes_t toks)
{
	idx_t i = astalloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTBLK;
	ast->kids[i].lhs = AST_EMPTY;
	ast->kids[i].rhs = i;

	if (toks.kinds[toksidx++] != LEXLBRACE)
		err("parser: Expected left brace");

	if (toks.kinds[toksidx] != LEXRBRACE) {
		idx_t stmt = parsestmt(ast, aux, toks);
		ast->kids[i].lhs = ast->kids[i].rhs = stmt;
	}

	while (toks.kinds[toksidx] != LEXRBRACE) {
		idx_t stmt = parsestmt(ast, aux, toks);
		ast->kids[i].rhs = stmt;
	}

	toksidx++; /* Eat rbrace */
	return i;
}

idx_t
parsedecl(ast_t *ast, aux_t *aux, lexemes_t toks, bool toplvl)
{
	idx_t i = astalloc(ast), j = aux->len++;

	if (aux->len > aux->cap) {
		aux->cap *= 2;
		aux->buf = bufalloc(aux->buf, aux->cap, sizeof(*aux->buf));
	}

	bool static_kw = toks.kinds[toksidx] == LEXIDENT
	              && strview_eq(SV("static"), toks.strs[toksidx]);
	if (static_kw) {
		toksidx++;
		if (toplvl)
			err("parser: The ‘static’ doesn’t apply to global variables");
	}

	aux->buf[j].decl.isundef = false;
	if (toplvl && toks.kinds[toksidx] == LEXIDENT
	    && strview_eq(SV("pub"), toks.strs[toksidx]))
	{
		aux->buf[j].decl.ispub = true;
		ast->lexemes[i] = ++toksidx;
	} else {
		aux->buf[j].decl.ispub = false;
		ast->lexemes[i] = toksidx;
	}

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected identifier");
	if (toks.kinds[toksidx++] != LEXCOLON)
		err("parser: Expected colon");

	aux->buf[j].decl.type = toks.kinds[toksidx] == LEXIDENT
	                          ? parsetype(ast, aux, toks)
	                          : AST_EMPTY;
	ast->kids[i].lhs = j;

	switch (toks.kinds[toksidx++]) {
	case LEXSEMI:
		if (aux->buf[j].decl.type == AST_EMPTY)
			err("parser: No type provided in non-assigning declaration");
		ast->kinds[i] = ASTDECL;
		ast->kids[i].rhs = AST_EMPTY;
		aux->buf[j].decl.isstatic = toplvl || static_kw;
		return i;
	case LEXCOLON:
		ast->kinds[i] = ASTCDECL;
		aux->buf[j].decl.isstatic = false;
		if (static_kw)
			err("parser: Cannot declare a constant symbol ‘static’");
		break;
	case LEXEQ:
		ast->kinds[i] = ASTDECL;
		aux->buf[j].decl.isstatic = toplvl || static_kw;
		break;
	default:
		err("parser: Expected colon, equals, or semicolon");
	}

	idx_t rhs;
	bool func = false;

	switch (toks.kinds[toksidx]) {
	case LEXLPAR:
		if (!(func = isfunc(toks)))
			goto not_fn;
		if (ast->kinds[i] == ASTDECL)
			err("Cannot assign function to mutable variable");
		rhs = parsefunc(ast, aux, toks);
		break;
	case LEXELIP:
		toksidx++;
		if (ast->kinds[i] == ASTCDECL)
			err("parser: Cannot assign to ‘…’ in constant declaration");
		rhs = AST_EMPTY;
		aux->buf[j].decl.isundef = true;
		break;
	default:
not_fn:
		rhs = parseexpr(ast, toks, 0);
	}

	ast->kids[i].rhs = rhs;
	if (!func && toks.kinds[toksidx++] != LEXSEMI)
		err("parser: Expected semicolon");

	return i;
}

idx_t
parsefunc(ast_t *ast, aux_t *aux, lexemes_t toks)
{
	idx_t i = astalloc(ast);
	ast->lexemes[i] = toksidx;

	assert(toks.kinds[toksidx] == LEXLPAR);

	ast->kinds[i] = ASTFN;
	idx_t lhs = parseproto(ast, aux, toks);
	idx_t rhs = parseblk(ast, aux, toks);
	ast->kids[i].lhs = lhs;
	ast->kids[i].rhs = rhs;

	return i;
}

idx_t
parseexpratom(ast_t *ast, lexemes_t toks)
{
	/* We handle parenthesised expressions up here because we don’t want
	   to allocate a new AST node for them */
	if (toks.kinds[toksidx] == LEXLPAR) {
		toksidx++;
		idx_t i = parseexpr(ast, toks, 0);
		if (toks.kinds[toksidx++] != LEXRPAR)
			err("parser: Expected closing parenthesis after expression");
		return i;
	}

	idx_t i = astalloc(ast);

	ast->lexemes[i] = toksidx;

	switch (toks.kinds[toksidx++]) {
	case LEXNUM:
		ast->kinds[i] = ASTNUMLIT;
		break;
	case LEXIDENT:
		ast->kinds[i] = ASTIDENT;
		break;
	case LEXPLUS:
		/* Unary plus is kind of a fake syntactic construct.  We would
		   ideally just pretend like it doesn’t exist, but allow it in
		   the syntax to be consistent with unary negation.  We can’t
		   just ignoring it in parsing though, because we need to
		   disallow the statements ‘x := 0; +x = 1;’ */
		ast->kinds[i] = ASTUNPLUS;
		ast->kids[i].rhs = parseexpratom(ast, toks);
		break;
	case LEXMINUS:
		ast->kinds[i] = ASTUNNEG;
		ast->kids[i].rhs = parseexpratom(ast, toks);
		break;
	case LEXTILDE:
		ast->kinds[i] = ASTUNCMPL;
		ast->kids[i].rhs = parseexpratom(ast, toks);
		break;
	default:
		err("parser: Invalid expression leaf");
	}

	if (toks.kinds[toksidx] == '(') {
		toksidx++;
		if (toks.kinds[toksidx++] != ')')
			err("parser: Expected closing parenthesis");

		idx_t j = astalloc(ast);
		ast->kinds[j]   = ast->kinds[i];
		ast->kids[j]    = ast->kids[i];
		ast->lexemes[j] = ast->lexemes[i];
		ast->kinds[i]   = ASTFUNCALL;
		ast->kids[i]    = (pair_t){j, AST_EMPTY};
	}

	return i;
}

idx_t
parseexprinc(ast_t *ast, lexemes_t toks, idx_t lhs, int minprec)
{
	static const int prectbl[UINT8_MAX + 1] = {
		[LEXBANGEQ] = 3,
		[LEXEQ_DBL] = 3,
		['+'] = 4,
		['-'] = 4,
		['|'] = 4,
		['~'] = 4,
		['%'] = 5,
		['&'] = 5,
		['*'] = 5,
		['/'] = 5,
		[LEXLANGL_DBL] = 5,
		[LEXRANGL_DBL] = 5,
	};

	uint8_t op = toks.kinds[toksidx];
	int nxtprec = prectbl[op];
	if (nxtprec <= minprec)
		return lhs;
	toksidx++;
	idx_t i = astalloc(ast);
	idx_t rhs = parseexpr(ast, toks, nxtprec);
	ast->kinds[i] = op;
	ast->lexemes[i] = toksidx - 1;
	ast->kids[i].lhs = lhs;
	ast->kids[i].rhs = rhs;
	return i;
}

idx_t
parseexpr(ast_t *ast, lexemes_t toks, int minprec)
{
	idx_t lhs = parseexpratom(ast, toks);

	for (;;) {
		idx_t rhs = parseexprinc(ast, toks, lhs, minprec);
		if (lhs == rhs)
			break;
		lhs = rhs;
	}

	return lhs;
}

idx_t
parseproto(ast_t *ast, aux_t *aux, lexemes_t toks)
{
	idx_t i = astalloc(ast);
	ast->lexemes[i] = toksidx;
	ast->kinds[i] = ASTFNPROTO;
	ast->kids[i].lhs = AST_EMPTY;

	if (toks.kinds[toksidx++] != LEXLPAR)
		err("parser: Expected left parenthesis");
	if (toks.kinds[toksidx++] != LEXRPAR)
		err("parser: Expected right parenthesis");

	idx_t rhs = toks.kinds[toksidx] == LEXIDENT ? parsetype(ast, aux, toks)
	                                             : AST_EMPTY;
	ast->kids[i].rhs = rhs;
	return i;
}

idx_t
parsestmt(ast_t *ast, aux_t *aux, lexemes_t toks)
{
	idx_t i;

	strview_t sv = toks.strs[toksidx];
	if (strview_eq(SV("return"), sv)) {
		i = astalloc(ast);
		ast->lexemes[i] = toksidx++;
		ast->kinds[i] = ASTRET;

		idx_t rhs = toks.kinds[toksidx] != LEXSEMI ? parseexpr(ast, toks, 0)
		                                           : AST_EMPTY;
		ast->kids[i].rhs = rhs;
		if (toks.kinds[toksidx++] != LEXSEMI)
			err("parser: Expected semicolon");
	} else if (strview_eq(SV("static"), sv)
	           || toks.kinds[toksidx + 1] == LEXCOLON)
	{
		i = parsedecl(ast, aux, toks, false);
	} else if (strview_eq(SV("pub"), sv)) {
		err("parser: Attempted to use ‘pub’ outside of a global scope");
	} else /* assignment or funcall */ {
		idx_t lhs, rhs;
		i = astalloc(ast);
		lhs = parseexpr(ast, toks, 0);

		if (ast->kinds[lhs] != ASTFUNCALL || toks.kinds[toksidx + 1] == LEXEQ) {
			if (toks.kinds[toksidx++] != LEXEQ)
				err("parser: Expected equals");
			rhs = parseexpr(ast, toks, 0);
			if (toks.kinds[toksidx++] != LEXSEMI)
				err("parser: Expected semicolon");
			ast->kinds[i] = ASTASIGN;
			ast->kids[i].lhs = lhs;
			ast->kids[i].rhs = rhs;
		} else {
			if (toks.kinds[toksidx++] != LEXSEMI)
				err("parser: Expected semicolon");
			ast->kinds[i] = ASTCALLSTMT;
		}
	}

	return i;
}

idx_t
parsetype(ast_t *ast, aux_t *aux, lexemes_t toks)
{
	(void)aux;

	idx_t i = astalloc(ast);
	ast->kinds[i] = ASTTYPE;
	ast->lexemes[i] = toksidx;

	if (toks.kinds[toksidx++] != LEXIDENT)
		err("parser: Expected type");

	return i;
}

bool
isfunc(lexemes_t toks)
{
	assert(toks.kinds[toksidx] == LEXLPAR);

	if (toks.kinds[toksidx + 1] == LEXRPAR)
		return true;
	for (size_t i = toksidx + 1;; i++) {
		switch (toks.kinds[i]) {
		case LEXCOLON:
			return true;
		case LEXEOF:
		case LEXLPAR:
		case LEXMINUS:
		case LEXPERC:
		case LEXPLUS:
		case LEXRPAR:
		case LEXSLASH:
		case LEXSTAR:
			return false;
		}
	}
}

ast_t
mkast(void)
{
	ast_t soa;

	static_assert(AST_DFLT_CAP * sizeof(*soa.kinds) % alignof(idx_t) == 0,
	              "Additional padding is required to properly align LEXEMES");
	static_assert(AST_DFLT_CAP * (sizeof(*soa.kinds) + sizeof(*soa.lexemes))
	                      % alignof(pair_t)
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
astresz(ast_t *soa)
{
	size_t ncap, pad1, pad2, newsz;
	ptrdiff_t lexemes_off, kids_off;

	lexemes_off = (char *)soa->lexemes - (char *)soa->kinds;
	kids_off = (char *)soa->kids - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for
	   overflow becomes pretty trivial */
	if (unlikely((soa->cap >> (SIZE_WDTH - 1)) != 0)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->lexemes is properly aligned */
	pad1 = alignof(idx_t) - ncap % alignof(idx_t);
	if (pad1 == alignof(idx_t))
		pad1 = 0;

	/* Ensure that soa->kids is properly aligned */
	pad2 = alignof(pair_t)
	     - (ncap * (1 + sizeof(idx_t)) + pad1) % alignof(pair_t);
	if (pad2 == alignof(pair_t))
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

idx_t
astalloc(ast_t *soa)
{
	if (unlikely(soa->len == soa->cap))
		astresz(soa);
	return soa->len++;
}
