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
#include "bitset.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "strview.h"
#include "tables.h"
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
	arena_t   *a;
	scratch_t *s;

	ast_t      ast;
	aux_t      aux;
	bitset_t  *cnst;
	fold_t    *folds;
	lexemes_t  toks;
	scopes_t   scps;
	typetab_t *ttab;
	type_t   **types;

	/* The return type of the function being analyzed */
	type_t *fnret;

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

/* Perform static analysis over the AST */
static void analyzeast(struct azctx *)
	__attribute__((nonnull));

/* Perform constant folding over the AST */
static void constfold(struct azctx *)
	__attribute__((nonnull));

/* Perform a pass over the entire AST and generate an array of symbol
   tables, one for each scope in the program.  These tables are stored in
   the provided CTX. */
static void gensymtabs(struct azctx *ctx)
	__attribute__((nonnull));

/* Find all the unordered symbols in the scope delimited by the inclusive
   indicies BEG and END in the AST, and accumulate them into a symbol
   table appended to the symbol table list.  UP is the index of the
   previous scopes symbol table in the symbol table list. */
static void find_unordered_syms(struct azctx *, idx_t up, idx_t beg, idx_t end);

typedef idx_t analyzer(struct azctx *, idx_t)
	__attribute__((nonnull));
static analyzer analyzeblk, analyzedecl, analyzeexpr, analyzefn, analyzestmt,
	constfoldblk, constfolddecl, constfoldstmt;

/* Perform constant-folding on the expression at index I in the AST, and
   assert that the resulting constant can be represented by type T. */
static idx_t constfoldexpr(struct azctx *, type_t *T, idx_t i)
	__attribute__((nonnull));

/* Assert if the types T1 and T2 are compatible with each other */
static bool typecompat(type_t *t1, type_t *t2)
	__attribute__((nonnull));

/* Check if the statement at node I in the AST returns from the function */
static bool returns(ast_t, idx_t i);

static bool memzeroed(void *, size_t)
	__attribute__((nonnull));

enum {
	PRIM_INT8,
	PRIM_INT16,
	PRIM_INT32,
	PRIM_INT64,
	PRIM_INT128,
	PRIM_INT,

	PRIM_UINT8,
	PRIM_UINT16,
	PRIM_UINT32,
	PRIM_UINT64,
	PRIM_UINT128,
	PRIM_UINT,

	PRIM_BOOL,

	PRIM_RUNE,

	PRIM_F16,
	PRIM_F32,
	PRIM_F64,
	PRIM_F128,

	_PRIM_CNT,
};

static struct {
	strview_t name;
	type_t t;
} primitives[] = {
	[PRIM_INT]    = {SVC("int"),  {.kind = TYPE_NUM, .size =  8, .issigned = true}},
	[PRIM_INT8]   = {SVC("i8"),   {.kind = TYPE_NUM, .size =  1, .issigned = true}},
	[PRIM_INT16]  = {SVC("i16"),  {.kind = TYPE_NUM, .size =  2, .issigned = true}},
	[PRIM_INT32]  = {SVC("i32"),  {.kind = TYPE_NUM, .size =  4, .issigned = true}},
	[PRIM_INT64]  = {SVC("i64"),  {.kind = TYPE_NUM, .size =  8, .issigned = true}},
	[PRIM_INT128] = {SVC("i128"), {.kind = TYPE_NUM, .size = 16, .issigned = true}},

	[PRIM_UINT]    = {SVC("uint"), {.kind = TYPE_NUM, .size =  8}},
	[PRIM_UINT8]   = {SVC("u8"),   {.kind = TYPE_NUM, .size =  1}},
	[PRIM_UINT16]  = {SVC("u16"),  {.kind = TYPE_NUM, .size =  2}},
	[PRIM_UINT32]  = {SVC("u32"),  {.kind = TYPE_NUM, .size =  4}},
	[PRIM_UINT64]  = {SVC("u64"),  {.kind = TYPE_NUM, .size =  8}},
	[PRIM_UINT128] = {SVC("u128"), {.kind = TYPE_NUM, .size = 16}},

	[PRIM_BOOL] = {SVC("bool"), {.kind = TYPE_BOOL, .size = 1}},

	[PRIM_RUNE] = {SVC("rune"), {.kind = TYPE_NUM, .size = 4, .issigned = true}},

	[PRIM_F16]  = {SVC("f16"),  {.kind = TYPE_NUM, .size =  2, .isfloat = true}},
	[PRIM_F32]  = {SVC("f32"),  {.kind = TYPE_NUM, .size =  4, .isfloat = true}},
	[PRIM_F64]  = {SVC("f64"),  {.kind = TYPE_NUM, .size =  8, .isfloat = true}},
	[PRIM_F128] = {SVC("f128"), {.kind = TYPE_NUM, .size = 16, .isfloat = true}},
};

static type_t NOT_CHECKED  = {.kind = TYPE_CHECKING};
static type_t UNTYPED_BOOL = {.kind = TYPE_BOOL};
static type_t UNTYPED_INT  = {.kind = TYPE_NUM};
static type_t UNTYPED_FLT  = {.kind = TYPE_NUM, .isfloat = true};

type_t **
analyzeprog(ast_t ast, aux_t aux, lexemes_t toks, arena_t *a, scope_t **scps,
            fold_t **folds, bitset_t **cnst)
{
	struct azctx ctx = {
		.a = a,
		.s = &(scratch_t){0},
		.ast = ast,
		.aux = aux,
		.toks = toks,
	};

	for (size_t i = 0; i < lengthof(primitives); i++) {
		*typetab_insert(&ctx.ttab, primitives[i].name, a) =
			(type_t *)&primitives[i].t;
	}

	if ((ctx.types = calloc(ctx.ast.len, sizeof(*ctx.types))) == NULL)
		err("calloc:");
	ctx.cnst = mkbitset(ctx.a, ctx.ast.len);

	gensymtabs(&ctx);
	analyzeast(&ctx);

	if ((ctx.folds = calloc(ctx.ast.len, sizeof(fold_t))) == NULL)
		err("calloc:");
	constfold(&ctx);
	*cnst = ctx.cnst;
	*folds = ctx.folds;
	*scps = ctx.scps.buf;
	return ctx.types;
}

void
gensymtabs(struct azctx *ctx)
{
	ctx->scps.cap = 32;
	ctx->scps.buf = bufalloc(NULL, ctx->scps.cap, sizeof(*ctx->scps.buf));
	find_unordered_syms(ctx, 0, 0, ctx->ast.len - 1);
}

void
find_unordered_syms(struct azctx *ctx, idx_t up, idx_t beg, idx_t end)
{
	if (ctx->scps.len == ctx->scps.cap) {
		ctx->scps.cap *= 2;
		ctx->scps.buf = bufalloc(ctx->scps.buf, ctx->scps.cap,
		                         sizeof(*ctx->scps.buf));
	}

	scope_t *scp = ctx->scps.buf + ctx->scps.len++;
	*scp = (scope_t){.i = beg, .up = up};

	for (idx_t i = beg; likely(i <= end); i++) {
		bool isstatic = ctx->ast.kinds[i] <= _AST_DECLS_END
		             && ctx->aux.buf[ctx->ast.kids[i].lhs].decl.isstatic;
		bool isconst = ctx->ast.kinds[i] == ASTCDECL;

		if (isstatic || isconst) {
			strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];
			symval_t *p = symtab_insert(&scp->map, sv, ctx->a);
			if (p->exists) {
				err("analyzer: Symbol ‘%.*s’ declared multiple times",
				    SV_PRI_ARGS(sv));
			}
			p->i = i;
			p->exists = true;
		} else if (ctx->ast.kinds[i] == ASTBLK) {
			pair_t p = ctx->ast.kids[i];
			find_unordered_syms(ctx, beg, p.lhs, p.rhs);
			i = p.rhs;
		}
	}
}

void
analyzeast(struct azctx *ctx)
{
	for (idx_t i = 0; likely(i < ctx->ast.len); i = fwdnode(ctx->ast, i)) {
		assert(ctx->ast.kinds[i] <= _AST_DECLS_END);
		(void)analyzedecl(ctx, i);
	}
}

idx_t
analyzedecl(struct azctx *ctx, idx_t i)
{
	strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];

	bool isconst = ctx->ast.kinds[i] == ASTCDECL;
	bool isundef = ctx->aux.buf[ctx->ast.kids[i].lhs].decl.isundef;
	bool isstatic = ctx->ast.kinds[i] <= _AST_DECLS_END
	             && ctx->aux.buf[ctx->ast.kids[i].lhs].decl.isstatic;

	if (isconst)
		SETBIT(ctx->cnst, i);

	if (isstatic && isundef)
		err("analyzer: Static variables may not be undefined");

	if (!isconst && !isstatic) {
		symval_t *sym = symtab_insert(&ctx->scps.buf[ctx->si].map, sv, ctx->a);
		if (sym->exists) {
			err("analyzer: Variable ‘%.*s’ declared multiple times",
			    SV_PRI_ARGS(sv));
		} else {
			sym->i = i;
			sym->exists = true;
		}
	}

	ctx->types[i] = &NOT_CHECKED;

	pair_t p = ctx->ast.kids[i];
	type_t *ltype, *rtype;
	ltype = rtype = NULL;

	idx_t typeidx = ctx->aux.buf[p.lhs].decl.type;
	assert(typeidx != AST_EMPTY || p.rhs != AST_EMPTY);

	idx_t ni;

	if (typeidx != AST_EMPTY) {
		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[typeidx]];
		type_t **t = typetab_insert(&ctx->ttab, sv, NULL);
		if (t == NULL)
			err("analyzer: Undeclared type ‘%.*s’", SV_PRI_ARGS(sv));
		ltype = *t;
	}
	if (p.rhs != AST_EMPTY) {
		struct azctx nctx = *ctx;
		nctx.decl = sv;
		ni = analyzeexpr(&nctx, p.rhs);
		rtype = ctx->types[p.rhs];
	} else
		ni = fwdnode(ctx->ast, i);

	if (ltype == NULL) {
		ltype = rtype;
		if (ctx->ast.kinds[i] == ASTDECL && rtype->size == 0) {
			if (rtype == &UNTYPED_INT)
				ltype = &primitives[PRIM_INT].t;
			else if (rtype == &UNTYPED_FLT)
				ltype = &primitives[PRIM_F64].t;
			else if (rtype == &UNTYPED_BOOL)
				ltype = &primitives[PRIM_BOOL].t;
			else
				__builtin_unreachable();
		}
	} else if (rtype != NULL && !typecompat(ltype, rtype))
		err("analyzer: Type mismatch");

	ctx->types[i] = ltype;
	return ni;
}

idx_t
analyzestmt(struct azctx *ctx, idx_t i)
{
	switch (ctx->ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return analyzedecl(ctx, i);
	case ASTASIGN: {
		pair_t p = ctx->ast.kids[i];
		(void)analyzeexpr(ctx, p.lhs);
		/* TODO: Allow assignments to expressions returning pointer types */
		if (ctx->ast.kinds[p.lhs] != ASTIDENT)
			err("analyzer: Assignments may only be made to identifiers");
		idx_t ni = analyzeexpr(ctx, p.rhs);
		if (!typecompat(ctx->types[p.lhs], ctx->types[p.rhs]))
			err("analyzer: Assignment type mismatch");
		ctx->types[i] = ctx->types[p.lhs];
		return ni;
	}
	case ASTRET: {
		idx_t expr = ctx->ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			if (ctx->fnret != NULL)
				err("analyzer: Missing return value");
			return i + 1;
		} else if (ctx->fnret == NULL)
			err("analyzer: Function has no return value");

		idx_t ni = analyzeexpr(ctx, expr);
		if (!typecompat(ctx->fnret, ctx->types[expr]))
			err("analyzer: Return type mismatch");
		ctx->types[i] = ctx->fnret;
		return ni;
	}
	case ASTCALLSTMT:
		return analyzeexpr(ctx, i + 1);
	default:
		__builtin_unreachable();
	}
}

idx_t
analyzeexpr(struct azctx *ctx, idx_t i)
{
	/* Create local copy */
	struct azctx nctx = *ctx;
	ctx = &nctx;

	switch (ctx->ast.kinds[i]) {
	case ASTNUMLIT: {
		SETBIT(ctx->cnst, i);
		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];
		ctx->types[i] = memchr(sv.p, '.', sv.len) != NULL ? &UNTYPED_FLT
		                                                  : &UNTYPED_INT;
		return fwdnode(ctx->ast, i);
	}
	case ASTIDENT: {
		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx->decl) && ctx->si > 0)
			ctx->si--;

		for (idx_t lvl = ctx->si;;) {
			scope_t scp = ctx->scps.buf[lvl];
			symval_t *sym = symtab_insert(&scp.map, sv, NULL);

			if (sym == NULL) {
				if (lvl == 0)
					break;
				lvl = scp.up;
			} else {
				if (ctx->types[sym->i] == NULL) {
					ctx->si = lvl;
					(void)analyzedecl(ctx, sym->i);
				} else if (ctx->types[sym->i]->kind == TYPE_CHECKING) {
					err("analyzer: Circular definition of ‘%.*s’",
					    SV_PRI_ARGS(sv));
				}

				SET_DST_IF_SRC(ctx->cnst, i, sym->i);
				ctx->types[i] = ctx->types[sym->i];
				return fwdnode(ctx->ast, i);
			}
		}

		err("analyzer: Unknown symbol ‘%.*s’", SV_PRI_ARGS(sv));
	}
	case ASTUNCMPL:
	case ASTUNNEG: {
		idx_t ni, rhs;
		rhs = ctx->ast.kids[i].rhs;
		ni = analyzeexpr(ctx, rhs);
		SET_DST_IF_SRC(ctx->cnst, i, rhs);
		type_t *t = ctx->types[rhs];
		if (ctx->ast.kinds[i] == ASTUNNEG
		    && (t->kind != TYPE_NUM || !t->issigned))
		{
			err("analyzer: Unary negation is reserved for signed numeric "
			    "types");
		} else if (ctx->ast.kinds[i] == ASTUNCMPL
		           && (t->kind != TYPE_NUM || t->isfloat))
		{
			err("analyzer: Unary negation is reserved for numeric integer "
			    "types");
		}
		ctx->types[i] = t;
		return ni;
	}
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
	case ASTBINXOR: {
		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;
		(void)analyzeexpr(ctx, lhs);
		idx_t ni = analyzeexpr(ctx, rhs);

		if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs))
			SETBIT(ctx->cnst, i);

		bool isshift = ctx->ast.kinds[i] == ASTBINSHL
		            || ctx->ast.kinds[i] == ASTBINSHR;
		if (!isshift && !typecompat(ctx->types[lhs], ctx->types[rhs]))
			err("analyzer: Binary oprand type mismatch");

		static const bool int_only[UINT8_MAX + 1] = {
			[ASTBINAND] = true, [ASTBINIOR] = true, [ASTBINMOD] = true,
			[ASTBINSHL] = true, [ASTBINSHR] = true, [ASTBINXOR] = true,
		};

		static const bool logical[UINT8_MAX + 1] = {
			[ASTBINEQ] = true,
			[ASTBINNEQ] = true,
		};

		if (int_only[ctx->ast.kinds[i]]
		    && (ctx->types[lhs]->kind != TYPE_NUM || ctx->types[lhs]->isfloat
		        || ctx->types[rhs]->kind != TYPE_NUM
		        || ctx->types[rhs]->isfloat))
		{
			err("analyzer: Operation not defined for non-integer types");
		}

		/* In the expression ‘x ⋆ y’ where ⋆ is a binary operator, the
		   expression has type T if both x and y have type T.  If one of
		   x or y is a sized type and the other isn’t, then the
		   expression has the type of the sized type.

		   Additionally if both x and y are unsized types but one is a
		   floating-point type and the other isn’t, the type of the
		   expression is an unsized floating-point type.

		   There is an exception for the left- and right shift operators.
		   Expressions for these operators always take the type of x, and
		   y can be any integer type. */
		if (isshift) {
			ctx->types[i] = ctx->types[lhs];
		} else if (logical[ctx->ast.kinds[i]]) {
			ctx->types[i] = TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)
			                  ? &UNTYPED_BOOL
			                  : &primitives[PRIM_BOOL].t;
		} else if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)) {
			ctx->types[i] = ctx->types[ctx->types[lhs]->isfloat ? lhs : rhs];
		} else {
			ctx->types[i] = ctx->types[TESTBIT(ctx->cnst, lhs) ? rhs : lhs];
		}
		return ni;
	}
	case ASTFN:
		return analyzefn(ctx, i);
	case ASTFUNCALL: {
		/* TODO: Support function arguments */
		assert(ctx->ast.kids[i].rhs == AST_EMPTY);
		idx_t ni, lhs = ctx->ast.kids[i].lhs;
		ni = analyzeexpr(ctx, lhs);
		ctx->types[i] = ctx->types[lhs]->ret;
		if (ctx->types[i] == NULL && ctx->ast.kinds[i - 1] != ASTCALLSTMT)
			err("analyzer: Function without return type cannot be called in expression");
		return ni;
	}
	default:
		__builtin_unreachable();
	}
}

idx_t
analyzefn(struct azctx *ctx, idx_t i)
{
	type_t t = {.kind = TYPE_FN};
	pair_t p = ctx->ast.kids[i];

	/* Create local copy */
	struct azctx nctx = *ctx;
	ctx = &nctx;

	idx_t proto = p.lhs;
	idx_t ret = ctx->ast.kids[proto].rhs;
	if (ret != AST_EMPTY) {
		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[ret]];
		type_t **p = typetab_insert(&ctx->ttab, sv, NULL);
		if (p == NULL)
			err("analyzer: Undeclared type ‘%.*s’", SV_PRI_ARGS(sv));
		ctx->fnret = t.ret = *p;
		ctx->chkrets = true;
	} else
		ctx->fnret = NULL;
	*(ctx->types[i] = arena_new(ctx->a, type_t, 1)) = t;
	return analyzeblk(ctx, p.rhs);
}

idx_t
analyzeblk(struct azctx *ctx, idx_t i)
{
	/* Create local copy */
	struct azctx nctx = *ctx;
	ctx = &nctx;

	pair_t p = ctx->ast.kids[i];

	while (ctx->scps.buf[ctx->si].i != p.lhs)
		ctx->si++;

	bool chkrets = ctx->chkrets, hasret = false;
	ctx->chkrets = false;

	for (i = p.lhs; i <= p.rhs;) {
		if (chkrets && returns(ctx->ast, i))
			hasret = true;
		i = analyzestmt(ctx, i);
	}
	if (chkrets && !hasret)
		err("analyzer: Function doesn’t return on all paths");

	return i;
}

idx_t
constfoldstmt(struct azctx *ctx, idx_t i)
{
	switch (ctx->ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return constfolddecl(ctx, i);
	case ASTASIGN:
	case ASTRET:
		return constfoldexpr(ctx, ctx->types[i], ctx->ast.kids[i].rhs);
	case ASTCALLSTMT:
		return constfoldexpr(ctx, ctx->types[i], i + 1);
	default:
		__builtin_unreachable();
	}
}

idx_t
constfoldblk(struct azctx *ctx, idx_t i)
{
	/* Create local copy */
	struct azctx nctx = *ctx;
	ctx = &nctx;

	pair_t p = ctx->ast.kids[i];
	while (ctx->scps.buf[ctx->si].i != p.lhs)
		ctx->si++;
	for (i = p.lhs; i <= p.rhs; i = constfoldstmt(ctx, i))
		;
	return i;
}

idx_t
constfoldexpr(struct azctx *ctx, type_t *T, idx_t i)
{
	if (!memzeroed(ctx->folds[i].data, sizeof(ctx->folds[i].data)))
		return fwdnode(ctx->ast, i);

	idx_t ni;
	struct azctx nctx;

	switch (ctx->ast.kinds[i]) {
	case ASTFUNCALL:
		/* TODO: Constfold funcall params */
		ni = fwdnode(ctx->ast, i);
		break;
	case ASTFN:
		return constfoldblk(ctx, ctx->ast.kids[i].rhs);
	case ASTNUMLIT: {
		mpq_init(ctx->folds[i].q);

		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];
		char *buf = tmpalloc(ctx->s, sv.len + 1, 1);
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
			mpq_set_f(ctx->folds[i].q, x);
			mpf_clear(x);
		} else {
#if DEBUG
			int ret =
#endif
				mpq_set_str(ctx->folds[i].q, buf, 10);
			assert(ret == 0);
		}
		ni = fwdnode(ctx->ast, i);
		break;
	}
	case ASTIDENT: {
		/* Create local copy */
		nctx = *ctx;
		ctx = &nctx;

		strview_t sv = ctx->toks.strs[ctx->ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx->decl) && ctx->si > 0)
			ctx->si--;

		for (idx_t lvl = ctx->si;;) {
			scope_t scp = ctx->scps.buf[lvl];
			symval_t *sym = symtab_insert(&scp.map, sv, NULL);

			if (sym == NULL) {
				assert(lvl != 0);
				lvl = scp.up;
			} else {
				switch (ctx->ast.kinds[sym->i]) {
				case ASTDECL:
					return fwdnode(ctx->ast, i);
				case ASTCDECL: {
					idx_t expr = ctx->ast.kids[sym->i].rhs;
					assert(expr != AST_EMPTY);
					if (memzeroed(ctx->folds[i].data, sizeof(ctx->folds[i].data))) {
						ctx->si = lvl;
						(void)constfolddecl(ctx, sym->i);
					}
					if (ctx->types[i]->kind == TYPE_NUM)
						MPQCPY(ctx->folds[i].q, ctx->folds[expr].q);
					else if (ctx->types[i]->kind == TYPE_BOOL)
						ctx->folds[i].b = ctx->folds[expr].b;
					ni = fwdnode(ctx->ast, i);
					goto out;
				}
				default:
					__builtin_unreachable();
				}
			}
		}
out:
		break;
	}
	case ASTUNCMPL: {
		ni = constfoldexpr(ctx, ctx->types[i], ctx->ast.kids[i].rhs);
		if (TESTBIT(ctx->cnst, ctx->ast.kids[i].rhs))
			err("analyzer: Cannot perform bitwise complement of constant");
		break;
	}
	case ASTUNNEG: {
		idx_t rhs = ctx->ast.kids[i].rhs;
		ni = constfoldexpr(ctx, ctx->types[i], rhs);
		if (TESTBIT(ctx->cnst, rhs)) {
			MPQCPY(ctx->folds[i].q, ctx->folds[rhs].q);
			mpq_neg(ctx->folds[i].q, ctx->folds[i].q);
		}
		break;
	}
	case ASTBINADD:
	case ASTBINSUB:
	case ASTBINMUL: {
		static void (*const mpq_fns[UINT8_MAX + 1])(mpq_t, const mpq_t,
		                                            const mpq_t) = {
			['+'] = mpq_add,
			['-'] = mpq_sub,
			['*'] = mpq_mul,
		};
		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;
		(void)constfoldexpr(ctx, ctx->types[i], lhs);
		ni = constfoldexpr(ctx, ctx->types[i], rhs);
		if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)) {
			mpq_init(ctx->folds[i].q);
			mpq_fns[ctx->ast.kinds[i]](ctx->folds[i].q, ctx->folds[lhs].q,
			                           ctx->folds[rhs].q);
		}
		break;
	}
	case ASTBINDIV: {
		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;

		(void)constfoldexpr(ctx, ctx->types[i], lhs);
		ni = constfoldexpr(ctx, ctx->types[i], rhs);

		if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)) {
			mpq_init(ctx->folds[i].q);
			if (ctx->types[i]->isfloat)
				mpq_div(ctx->folds[i].q, ctx->folds[lhs].q, ctx->folds[rhs].q);
			else {
				mpz_tdiv_q(mpq_numref(ctx->folds[i].q),
				           mpq_numref(ctx->folds[lhs].q),
				           mpq_numref(ctx->folds[rhs].q));
			}
		}
		break;
	}
	case ASTBINAND:
	case ASTBINIOR:
	case ASTBINMOD:
	case ASTBINXOR: {
		static void (*const mpz_fns[UINT8_MAX + 1])(mpz_t, const mpz_t,
		                                            const mpz_t) = {
			['%'] = mpz_tdiv_q,
			['&'] = mpz_and,
			['|'] = mpz_ior,
			['~'] = mpz_xor,
		};

		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;

		(void)constfoldexpr(ctx, ctx->types[i], lhs);
		ni = constfoldexpr(ctx, ctx->types[i], rhs);

		if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)) {
			assert(MPQ_IS_WHOLE(ctx->folds[lhs].q));
			assert(MPQ_IS_WHOLE(ctx->folds[rhs].q));
			mpq_init(ctx->folds[i].q);
			mpz_fns[ctx->ast.kinds[i]](mpq_numref(ctx->folds[i].q),
			                           mpq_numref(ctx->folds[lhs].q),
			                           mpq_numref(ctx->folds[rhs].q));
		}
		break;
	}
	case ASTBINSHL:
	case ASTBINSHR: {
		static void (*const mpz_fns[UINT8_MAX + 1])(mpz_t, const mpz_t,
		                                            mp_bitcnt_t) = {
			[ASTBINSHL] = mpz_mul_2exp,
			[ASTBINSHR] = mpz_tdiv_q_2exp,
		};

		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;

		(void)constfoldexpr(ctx, ctx->types[lhs], lhs);
		ni = constfoldexpr(ctx, ctx->types[rhs], rhs);

		if (TESTBIT(ctx->cnst, rhs)) {
			if (mpq_sgn(ctx->folds[rhs].q) == -1)
				err("analyzer: Cannot shift by negative value");
			/* TODO: Assert that in X<<Y, Y is ≤ widthof(X) */
		}

		if (TESTBIT(ctx->cnst, lhs) && TESTBIT(ctx->cnst, rhs)) {
			mpz_ptr cur_z, lhs_z, rhs_z;
			cur_z = mpq_numref(ctx->folds[i].q);
			lhs_z = mpq_numref(ctx->folds[lhs].q);
			rhs_z = mpq_numref(ctx->folds[rhs].q);

			mpq_init(ctx->folds[i].q);
			if (mpz_cmp_ui(rhs_z, ULONG_MAX) > 0)
				err("analyzer: Shift oprand too large");
			mp_bitcnt_t shftcnt = mpz_get_ui(rhs_z);
			mpz_fns[ctx->ast.kinds[i]](cur_z, lhs_z, shftcnt);
		}
		break;
	}
	case ASTBINEQ:
	case ASTBINNEQ: {
		idx_t lhs, rhs;
		lhs = ctx->ast.kids[i].lhs;
		rhs = ctx->ast.kids[i].rhs;

		(void)constfoldexpr(ctx, ctx->types[i], lhs);
		ni = constfoldexpr(ctx, ctx->types[i], rhs);

		bool both_oprs_const = TESTBIT(ctx->cnst, lhs)
		                    && TESTBIT(ctx->cnst, rhs);
		bool can_constfold_ints = ctx->types[lhs]->kind == TYPE_NUM
		                       && both_oprs_const;
		bool can_constfold_bools = ctx->types[lhs]->kind == TYPE_BOOL
		                        && both_oprs_const;

		bool eq = can_constfold_ints
		            ? mpq_equal(ctx->folds[lhs].q, ctx->folds[rhs].q)
		        : can_constfold_bools ? ctx->folds[lhs].b == ctx->folds[rhs].b
		                              : false;

		if (can_constfold_ints || can_constfold_bools) {
			ctx->folds[i].set = true;
			ctx->folds[i].b = (ctx->ast.kinds[i] == ASTBINEQ && eq)
			               || (ctx->ast.kinds[i] == ASTBINNEQ && !eq);
		}
		break;
	}
	default:
		__builtin_unreachable();
	}

	/* Expression has no type, such as a function call with no return value */
	if (ctx->types[i] == NULL)
		return ni;

	if (ctx->types[i]->kind == TYPE_NUM && TESTBIT(ctx->cnst, i)
	    && !T->issigned && mpq_sgn(ctx->folds[i].q) == -1)
	{
		err("analyzer: Cannot convert negative value to unsigned type");
	}

	if (ctx->types[i]->kind == TYPE_NUM && TESTBIT(ctx->cnst, i)
	    && T->size != 0 && !T->isfloat)
	{
		if (!MPQ_IS_WHOLE(ctx->folds[i].q))
			err("analyzer: Invalid integer");

		int cmp;
		mpz_ptr num = mpq_numref(ctx->folds[i].q);
		if (T->size < sizeof(unsigned long)) {
			unsigned long x = 1UL << (T->size * 8 - T->issigned);
			cmp = mpz_cmp_ui(num, x - 1);
		} else {
			mpz_t x;
			mp_bitcnt_t bits = T->size * 8 - T->issigned;
			mpz_init_set_ui(x, 1);
			mpz_mul_2exp(x, x, bits);
			mpz_sub_ui(x, x, 1);
			cmp = mpz_cmp(num, x);
			mpz_clear(x);
		}
		if (cmp > 0)
			err("analyzer: Integer too large for datatype");
	}

	return ni;
}

idx_t
constfolddecl(struct azctx *ctx, idx_t i)
{
	if (ctx->ast.kids[i].rhs == AST_EMPTY)
		return fwdnode(ctx->ast, i);

	/* Create local copy */
	struct azctx nctx = *ctx;
	ctx = &nctx;
	ctx->decl = ctx->toks.strs[ctx->ast.lexemes[i]];

	return constfoldexpr(ctx, ctx->types[i], ctx->ast.kids[i].rhs);
}

void
constfold(struct azctx *ctx)
{
	for (idx_t i = 0; likely(i < ctx->ast.len); i = constfolddecl(ctx, i))
		assert(ctx->ast.kinds[i] <= _AST_DECLS_END);
}

bool
returns(ast_t ast, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTASIGN:
	case ASTCDECL:
	case ASTDECL:
		return false;
	case ASTRET:
		return true;
	}
	__builtin_unreachable();
}

bool
typecompat(type_t *lhs, type_t *rhs)
{
	if (lhs == rhs)
		return true;

	if (lhs->kind != rhs->kind)
		return false;

	/* Function types are compatible if they have the same parameter- and
	   return types */
	/* TODO: Compare the actual parameter types */
	if (lhs->kind == TYPE_FN)
		return lhs->paramcnt == rhs->paramcnt && lhs->ret == rhs->ret;

	/* Boolean types are only compatible with boolean types */
	if (lhs->kind == TYPE_BOOL)
		return true;

	/* At this point we only have numeric types left */

	/* Untyped numeric types are compatible with all numeric types */
	if (lhs->size == 0 || rhs->size == 0)
		return true;

	/* Two typed numeric types are only compatible if they have the same size
	   and sign and are either both integral or both floats */
	return lhs->issigned == rhs->issigned && lhs->isfloat == rhs->isfloat
	    && lhs->size == rhs->size;
}

bool
memzeroed(void *p, size_t n)
{
	uchar *m = p;
	return (*m == 0) && memcmp(m, m + 1, n - 1) == 0;
}
