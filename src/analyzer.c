#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <gmp.h>

#include "alloc.h"
#include "analyzer.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "strview.h"
#include "symtab.h"
#include "types.h"

#define LOG2_10       (3.321928)
#define MP_BITCNT_MAX ((mp_bitcnt_t)-1)

/* In debug builds we want to actually alloc a new mpq_t so that it’s
   easier to free memory without doing a double free */
#if DEBUG
#	define MPQCPY(x, y) do { mpq_init(x); mpq_set(x, y); } while (false)
#else
#	define MPQCPY(x, y) (*(x) = *(y))
#endif

typedef struct {
	scope_t *buf;
	size_t len, cap;
} scopes_t;

struct azctx {
	arena_t *a;

	/* The return type of the function being analyzed */
	type_t fnret;

	/* The name of the symbol being declared.  This is necessary to allow
	   for ‘X :: X’ to be treated as shadowing and not a circular
	   definition */
	strview_t decl;

	/* The index of the current scope in the scopes array */
	idx_t si;

	/* If we need to check for return statements.  Only true for the
	   outer body-block of a function that returns a value. */
	bool chkrets;
};

struct cfctx {
	arena_t *a;
	scratch_t *s;
	strview_t decl;
	idx_t si;
};

/* Perform static analysis over the AST */
static void analyzeast(scope_t *, type_t *, ast_t, aux_t, lexemes_t, arena_t *)
	__attribute__((nonnull));

/* Perform constant folding over the AST */
static void constfold(mpq_t *, scope_t *, type_t *, ast_t, lexemes_t, arena_t *,
                      scratch_t *)
	__attribute__((nonnull));

/* Perform a pass over the entire AST and return an array of symbol
   tables, one for each scope in the program */
static scope_t *gensymtabs(ast_t, aux_t, lexemes_t, arena_t *)
	__attribute__((returns_nonnull, nonnull));

/* Find all the unordered symbols in the scope delimited by the inclusive
   indicies BEG and END in the AST, and accumulate them into a symbol
   table appended to the symbol table list.  UP is the index of the
   previous scopes symbol table in the symbol table list. */
static void find_unordered_syms(scopes_t *, ast_t, aux_t, lexemes_t, idx_t up,
                                idx_t beg, idx_t end, arena_t *)
	__attribute__((nonnull));

typedef idx_t analyzer(struct azctx, scope_t *, type_t *, ast_t, aux_t,
                        lexemes_t, idx_t) __attribute__((nonnull));
typedef idx_t constfolder(struct cfctx, mpq_t *, scope_t *, type_t *, ast_t,
                           lexemes_t, idx_t) __attribute__((nonnull));

static analyzer analyzeblk, analyzedecl, analyzeexpr, analyzefn, analyzestmt;
static constfolder constfoldblk, constfolddecl, constfoldexpr, constfoldstmt;

static const type_t *typegrab(ast_t, lexemes_t, idx_t)
	__attribute__((returns_nonnull));
static bool typecompat(type_t, type_t);
static bool returns(ast_t, idx_t);

/* Defined in primitives.gperf */
const type_t *typelookup(const uchar *, size_t)
	__attribute__((nonnull));

void
analyzeprog(ast_t ast, aux_t aux, lexemes_t toks, arena_t *a, type_t **types,
            scope_t **scps, mpq_t **folds)
{
	if ((*types = calloc(ast.len, sizeof(**types))) == NULL)
		err("calloc:");

	*scps = gensymtabs(ast, aux, toks, a);
	analyzeast(*scps, *types, ast, aux, toks, a);

	scratch_t s = {0};
	if ((*folds = calloc(ast.len, sizeof(**folds))) == NULL)
		err("calloc:");
	constfold(*folds, *scps, *types, ast, toks, a, &s);
}

scope_t *
gensymtabs(ast_t ast, aux_t aux, lexemes_t toks, arena_t *a)
{
	scopes_t scps = {.cap = 32};
	scps.buf = bufalloc(NULL, scps.cap, sizeof(*scps.buf));
	find_unordered_syms(&scps, ast, aux, toks, 0, 0, ast.len - 1, a);
	return scps.buf;
}

void
find_unordered_syms(scopes_t *scps, ast_t ast, aux_t aux, lexemes_t toks,
                    idx_t up, idx_t beg, idx_t end, arena_t *a)
{
	if (scps->len == scps->cap) {
		scps->cap *= 2;
		scps->buf = bufalloc(scps->buf, scps->cap, sizeof(*scps->buf));
	}

	scope_t *scp = scps->buf + scps->len++;
	*scp = (scope_t){
		.i = beg,
		.up = up,
		.map = NULL,
	};

	for (idx_t i = beg; likely(i <= end); i++) {
		bool isstatic = ast.kinds[i] <= _AST_DECLS_END
		             && aux.buf[ast.kids[i].lhs].decl.isstatic;
		bool isconst = ast.kinds[i] == ASTCDECL;

		if (isstatic || isconst) {
			strview_t sv = toks.strs[ast.lexemes[i]];
			symval_t *p = symtab_insert(&scp->map, sv, a);
			if (p->exists) {
				err("analyzer: Symbol ‘%.*s’ declared multiple times",
				    SV_PRI_ARGS(sv));
			}
			p->i = i;
			p->exists = true;
		} else if (ast.kinds[i] == ASTBLK) {
			pair_t p = ast.kids[i];
			find_unordered_syms(scps, ast, aux, toks, beg, p.lhs, p.rhs, a);
			i = p.rhs;
		}
	}
}

const type_t *
typegrab(ast_t ast, lexemes_t toks, idx_t i)
{
	strview_t sv = toks.strs[ast.lexemes[i]];
	const type_t *tp = typelookup(sv.p, sv.len);
	if (tp == NULL)
		err("analyzer: Unknown type ‘%.*s’", (int)sv.len, sv.p);
	return tp;
}

void
analyzeast(scope_t *scps, type_t *types, ast_t ast, aux_t aux, lexemes_t toks,
           arena_t *a)
{
	struct azctx ctx = {.a = a};
	for (idx_t i = 0; likely(i < ast.len); i = fwdnode(ast, i)) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		analyzedecl(ctx, scps, types, ast, aux, toks, i);
	}
}

idx_t
analyzedecl(struct azctx ctx, scope_t *scps, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i)
{
	strview_t sv = toks.strs[ast.lexemes[i]];
	if (ctx.si > 0 && ast.kinds[i] == ASTDECL) {
		symval_t *sym = symtab_insert(&scps[ctx.si].map, sv, ctx.a);
		if (sym->exists) {
			err("analyzer: Variable ‘%.*s’ declared multiple times",
			    SV_PRI_ARGS(sv));
		} else {
			sym->i = i;
			sym->exists = true;
		}
	}

	types[i].kind = TYPE_CHECKING;

	pair_t p = ast.kids[i];
	type_t ltype, rtype;
	ltype.kind = TYPE_UNSET;

	idx_t typeidx = aux.buf[p.lhs].decl.type;
	assert(typeidx != AST_EMPTY || p.rhs != AST_EMPTY);

	idx_t ni;

	if (typeidx != AST_EMPTY)
		ltype = *typegrab(ast, toks, typeidx);
	if (p.rhs != AST_EMPTY) {
		ctx.decl = sv;
		ni = analyzeexpr(ctx, scps, types, ast, aux, toks, p.rhs);
		rtype = types[p.rhs];
	} else
		ni = fwdnode(ast, i);

	if (ltype.kind == TYPE_UNSET) {
		ltype = rtype;
		if (ast.kinds[i] == ASTDECL && rtype.size == 0)
			ltype.size = 8;
	} else if (!typecompat(ltype, rtype))
		err("analyzer: Type mismatch");

	types[i] = ltype;
	return ni;
}

idx_t
analyzestmt(struct azctx ctx, scope_t *scps, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return analyzedecl(ctx, scps, types, ast, aux, toks, i);
	case ASTRET: {
		idx_t expr = ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			if (ctx.fnret.kind != TYPE_UNSET)
				err("analyzer: Missing return value");
			return i + 1;
		} else if (ctx.fnret.kind == TYPE_UNSET)
			err("analyzer: Function has no return value");

		idx_t ni = analyzeexpr(ctx, scps, types, ast, aux, toks, expr);
		if (!typecompat(ctx.fnret, types[expr]))
			err("analyzer: Return type mismatch");
		types[i] = ctx.fnret;
		return ni;
	}
	default:
		__builtin_unreachable();
	}
}

idx_t
analyzeexpr(struct azctx ctx, scope_t *scps, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		strview_t sv = toks.strs[ast.lexemes[i]];
		types[i].kind = TYPE_NUM;
		types[i].size = 0;
		types[i].issigned = true;
		types[i].isfloat = memchr(sv.p, '.', sv.len) != NULL;
		return fwdnode(ast, i);
	}
	case ASTIDENT: {
		strview_t sv = toks.strs[ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx.decl) && ctx.si > 0)
			ctx.si--;

		for (idx_t lvl = ctx.si;;) {
			scope_t scp = scps[lvl];
			symval_t *sym = symtab_insert(&scp.map, sv, NULL);

			if (sym == NULL) {
				if (lvl == 0)
					break;
				lvl = scp.up;
			} else {
				switch (types[sym->i].kind) {
				case TYPE_UNSET:
					ctx.si = lvl;
					analyzedecl(ctx, scps, types, ast, aux, toks, sym->i);
					break;
				case TYPE_CHECKING:
					err("analyzer: Circular definition of ‘%.*s’",
					    SV_PRI_ARGS(sv));
				}

				types[i] = types[sym->i];
				return fwdnode(ast, i);
			}
		}

		err("analyzer: Unknown symbol ‘%.*s’", SV_PRI_ARGS(sv));
	}
	case ASTFN:
		return analyzefn(ctx, scps, types, ast, aux, toks, i);
	default:
		__builtin_unreachable();
	}
}

idx_t
analyzefn(struct azctx ctx, scope_t *scps, type_t *types, ast_t ast, aux_t aux,
          lexemes_t toks, idx_t i)
{
	type_t t = {.kind = TYPE_FN};
	pair_t p = ast.kids[i];

	idx_t proto = p.lhs;
	if (ast.kids[proto].rhs != AST_EMPTY) {
		t.ret = typegrab(ast, toks, ast.kids[proto].rhs);
		ctx.fnret = *t.ret;
		ctx.chkrets = true;
	} else
		ctx.fnret.kind = TYPE_UNSET;
	types[i] = t;
	return analyzeblk(ctx, scps, types, ast, aux, toks, p.rhs);
}

idx_t
analyzeblk(struct azctx ctx, scope_t *scps, type_t *types, ast_t ast, aux_t aux,
           lexemes_t toks, idx_t i)
{
	pair_t p = ast.kids[i];

	while (scps[ctx.si].i != p.lhs)
		ctx.si++;

	bool chkrets = ctx.chkrets, hasret = false;
	ctx.chkrets = false;

	for (i = p.lhs; i <= p.rhs;) {
		if (chkrets && returns(ast, i))
			hasret = true;
		i = analyzestmt(ctx, scps, types, ast, aux, toks, i);
	}
	if (chkrets && !hasret)
		err("analyzer: Function doesn’t return on all paths");

	return i;
}

idx_t
constfoldstmt(struct cfctx ctx, mpq_t *folds, scope_t *scps, type_t *types,
              ast_t ast, lexemes_t toks, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return constfolddecl(ctx, folds, scps, types, ast, toks, i);
	case ASTRET:
		return constfoldexpr(ctx, folds, scps, types, ast, toks,
		                     ast.kids[i].rhs);
	default:
		__builtin_unreachable();
	}
}

idx_t
constfoldblk(struct cfctx ctx, mpq_t *folds, scope_t *scps, type_t *types,
             ast_t ast, lexemes_t toks, idx_t i)
{
	pair_t p = ast.kids[i];
	while (scps[ctx.si].i != p.lhs)
		ctx.si++;
	for (i = p.lhs; i <= p.rhs;
	     i = constfoldstmt(ctx, folds, scps, types, ast, toks, i))
		;

	return i;
}

idx_t
constfoldexpr(struct cfctx ctx, mpq_t *folds, scope_t *scps, type_t *types,
              ast_t ast, lexemes_t toks, idx_t i)
{
	if (MPQ_IS_INIT(folds[i]))
		return fwdnode(ast, i);

	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		mpq_init(folds[i]);

		strview_t sv = toks.strs[ast.lexemes[i]];
		char *buf = tmpalloc(ctx.s, sv.len + 1, 1);
		size_t len = 0;

		bool isfloat = false;

		for (size_t i = 0; i < sv.len; i++) {
			if (sv.p[i] == '.') {
				isfloat = true;
				buf[len++] = sv.p[i];
			}
			if (isdigit(sv.p[i]))
				buf[len++] = sv.p[i];
		}
		buf[len] = 0;

		if (isfloat) {
			/* TODO: Is this correct?  It seems to work… but I don’t know
			   if there is a better way to do this; sometimes the
			   precision is a few bits more than it could be. */
			mpf_t x;
			double prec = ceil((sv.len - 1) * LOG2_10);
			mpf_init2(x, MIN(MP_BITCNT_MAX, (mp_bitcnt_t)prec));
#if DEBUG
			int ret =
#endif
				mpf_set_str(x, buf, 10);
			assert(ret == 0);
			mpq_set_f(folds[i], x);
			mpf_clear(x);
		} else {
#if DEBUG
			int ret =
#endif
				mpq_set_str(folds[i], buf, 10);
			assert(ret == 0);
		}
		return fwdnode(ast, i);
	}
	case ASTIDENT: {
		strview_t sv = toks.strs[ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx.decl) && ctx.si > 0)
			ctx.si--;

		for (idx_t lvl = ctx.si;;) {
			scope_t scp = scps[lvl];
			symval_t *sym = symtab_insert(&scp.map, sv, NULL);

			if (sym == NULL) {
				assert(lvl != 0);
				lvl = scp.up;
			} else {
				switch (ast.kinds[sym->i]) {
				case ASTDECL:
					break;
				case ASTCDECL: {
					idx_t expr = ast.kids[sym->i].rhs;
					assert(expr != AST_EMPTY);
					MPQCPY(folds[i], folds[expr]);
					if (!MPQ_IS_INIT(folds[i])) {
						ctx.si = lvl;
						(void)constfolddecl(ctx, folds, scps, types,
						                    ast, toks, sym->i);
						MPQCPY(folds[i], folds[expr]);
						assert(MPQ_IS_INIT(folds[i]));
					}
					break;
				}
				default:
					__builtin_unreachable();
				}

				return fwdnode(ast, i);
			}
		}
	}
	case ASTFN:
		return constfoldblk(ctx, folds, scps, types, ast, toks,
		                    ast.kids[i].rhs);
	default:
		__builtin_unreachable();
	}
}

idx_t
constfolddecl(struct cfctx ctx, mpq_t *folds, scope_t *scps, type_t *types,
              ast_t ast, lexemes_t toks, idx_t i)
{
	if (ast.kids[i].rhs == AST_EMPTY)
		return fwdnode(ast, i);
	ctx.decl = toks.strs[ast.lexemes[i]];
	return constfoldexpr(ctx, folds, scps, types, ast, toks, ast.kids[i].rhs);
}

void
constfold(mpq_t *folds, scope_t *scps, type_t *types, ast_t ast, lexemes_t toks,
          arena_t *a, scratch_t *s)
{
	struct cfctx ctx = {.a = a, .s = s};
	for (idx_t i = 0; likely(i < ast.len);) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		i = constfolddecl(ctx, folds, scps, types, ast, toks, i);
	}
}

bool
returns(ast_t ast, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return false;
	case ASTRET:
		return true;
	}
	__builtin_unreachable();
}

bool
typecompat(type_t lhs, type_t rhs)
{
	/* Function types are compatible if they have the same parameter- and
	   return types */
	if (lhs.kind == TYPE_FN && rhs.kind == TYPE_FN)
		return lhs.paramcnt == rhs.paramcnt && lhs.ret == rhs.ret;
	if (lhs.kind == TYPE_FN || rhs.kind == TYPE_FN)
		return false;

	/* At this point we only have numeric types left */

	/* Untyped numeric types are compatible with all numeric types */
	if (lhs.size == 0 || rhs.size == 0)
		return true;

	/* Two typed numeric types are only compatible if they have the same size
	   and sign and are either both integral or both floats */
	return lhs.issigned == rhs.issigned && lhs.isfloat == rhs.isfloat
	    && lhs.size == rhs.size;
}
