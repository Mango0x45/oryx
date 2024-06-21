#include <assert.h>
#include <ctype.h>
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
#include "types.h"

/* A hashmap mapping symbol names to their indicies in the AST */
typedef struct symtab {
	struct symtab *child[4];
	struct strview key;
	idx_t_ val;
} symtab;

/* A dynamic array of scopes */
struct scopes {
	struct scope *buf;
	size_t len, cap;
};

/* Analyzer context; keeps track of the state of static analysis */
struct azctx {
	/* An arena allocator */
	arena *a;

	/* The return type of the function being analyzed */
	struct type fnret;

	/* The name of the symbol being declared.  This is necessary to allow
	   for ‘X :: X’ to be treated as shadowing and not a circular
	   definition */
	struct strview decl;

	/* The index of the current scope in the scopes array */
	idx_t_ si;

	/* If we need to check for return statements.  Only true for the
	   outer body-block of a function that returns a value. */
	bool chkrets;
};

struct cfctx {
	arena *a;
	struct strview decl;
	idx_t_ si;
};

static void analyzeast(struct scope *, struct type *, struct ast,
                       struct lexemes, arena *)
	__attribute__((nonnull));
static void constfold(mpq_t *, struct scope *, struct type *, struct ast,
                      struct lexemes, arena *)
	__attribute__((nonnull));

/* Perform a pass over the entire AST and return an array of symbol
   tables, one for each scope in the program */
static struct scope *gensymtabs(struct ast, struct lexemes, arena *)
	__attribute__((returns_nonnull, nonnull));

/* Find all the unordered symbols in the scope delimited by the inclusive
   indicies BEG and END in the AST, and accumulate them into a symbol
   table appended to the symbol table list.  UP is the index of the
   previous scopes symbol table in the symbol table list. */
static void find_unordered_syms(struct scopes *, struct ast, struct lexemes,
                                idx_t_ up, idx_t_ beg, idx_t_ end, arena *)
	__attribute__((nonnull));

typedef idx_t_ analyzer(struct azctx, struct scope *, struct type *, struct ast,
                        struct lexemes, idx_t_)
	__attribute__((nonnull));

static analyzer analyzeblk, analyzedecl, analyzeexpr, analyzefn, analyzestmt;

static const struct type *typegrab(struct ast, struct lexemes, idx_t_)
	__attribute__((returns_nonnull));
static bool typecompat(struct type, struct type);
static bool returns(struct ast, idx_t_);

/* Index the symbol table M with the key SV, returning a pointer to the
   value.  If no entry exists and A is non-null, a pointer to a newly
   allocated (and zeroed) value is returned, NULL otherwise. */
static idx_t_ *symtab_insert(symtab **m, struct strview sv, arena *a)
	__attribute__((nonnull(1)));

/* Defined in primitives.gperf */
const struct type *typelookup(const uchar *, size_t)
	__attribute__((nonnull));

void
analyzeprog(struct ast ast, struct lexemes toks, arena *a, struct type **types,
            struct scope **scps, mpq_t **folds)
{
	*types = bufalloc(NULL, ast.len, sizeof(**types));
	memset(*types, 0, ast.len * sizeof(**types));

	*scps = gensymtabs(ast, toks, a);
	analyzeast(*scps, *types, ast, toks, a);

	*folds = bufalloc(NULL, ast.len, sizeof(**folds));
	constfold(*folds, *scps, *types, ast, toks, a);
}

struct scope *
gensymtabs(struct ast ast, struct lexemes toks, arena *a)
{
	struct scopes scps = {.cap = 32};
	scps.buf = bufalloc(NULL, scps.cap, sizeof(*scps.buf));
	find_unordered_syms(&scps, ast, toks, 0, 0, ast.len - 1, a);
	return scps.buf;
}

void
find_unordered_syms(struct scopes *scps, struct ast ast, struct lexemes toks,
                    idx_t_ up, idx_t_ beg, idx_t_ end, arena *a)
{
	if (scps->len == scps->cap) {
		scps->cap *= 2;
		scps->buf = bufalloc(scps->buf, scps->cap, sizeof(*scps->buf));
	}

	struct scope *scp = scps->buf + scps->len++;
	*scp = (struct scope){
		.i = beg,
		.up = up,
		.map = NULL,
	};

	for (idx_t_ i = beg; likely(i <= end); i++) {
		bool globl_var = ast.kinds[i] <= _AST_DECLS_END && beg == 0;
		bool const_var = (ast.kinds[i] | 1) == ASTPCDECL;

		if (globl_var || const_var) {
			struct strview sv = toks.strs[ast.lexemes[i]];
			idx_t_ *p = symtab_insert(&scp->map, sv, a);
			if (*p != 0) {
				err("analyzer: Symbol ‘%.*s’ declared multiple times",
				    SV_PRI_ARGS(sv));
			}
			*p = i;
		} else if (ast.kinds[i] == ASTBLK) {
			struct pair p = ast.kids[i];
			find_unordered_syms(scps, ast, toks, beg, p.lhs, p.rhs, a);
			i = p.rhs;
		}
	}
}

const struct type *
typegrab(struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct strview sv = toks.strs[ast.lexemes[i]];
	const struct type *tp = typelookup(sv.p, sv.len);
	if (tp == NULL)
		err("analyzer: Unknown type ‘%.*s’", (int)sv.len, sv.p);
	return tp;
}

void
analyzeast(struct scope *scps, struct type *types, struct ast ast,
           struct lexemes toks, arena *a)
{
	struct azctx ctx = {.a = a};
	for (idx_t_ i = 0; likely(i < ast.len); i = fwdnode(ast, i)) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		analyzedecl(ctx, scps, types, ast, toks, i);
	}
}

idx_t_
analyzedecl(struct azctx ctx, struct scope *scps, struct type *types,
            struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct strview sv = toks.strs[ast.lexemes[i]];
	if (ctx.si > 0 && (ast.kinds[i] | 1) == ASTPDECL) {
		idx_t_ *ip = symtab_insert(&scps[ctx.si].map, sv, ctx.a);
		if (*ip == 0)
			*ip = i;
		else {
			err("analyzer: Variable ‘%.*s’ declared multiple times",
				SV_PRI_ARGS(sv));
		}
	}

	types[i].kind = TYPE_CHECKING;

	struct pair p = ast.kids[i];
	struct type ltype, rtype;
	ltype.kind = TYPE_UNSET;

	assert(p.lhs != AST_EMPTY || p.rhs != AST_EMPTY);

	idx_t_ ni;

	if (p.lhs != AST_EMPTY)
		ltype = *typegrab(ast, toks, p.lhs);
	if (p.rhs != AST_EMPTY) {
		ctx.decl = sv;
		ni = analyzeexpr(ctx, scps, types, ast, toks, p.rhs);
		rtype = types[p.rhs];
	} else
		ni = fwdnode(ast, i);

	if (ltype.kind == TYPE_UNSET)
		ltype = rtype;
	else if (!typecompat(ltype, rtype))
		err("analyzer: Type mismatch");

	types[i] = ltype;
	return ni;
}

idx_t_
analyzestmt(struct azctx ctx, struct scope *scps, struct type *types,
            struct ast ast, struct lexemes toks, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return analyzedecl(ctx, scps, types, ast, toks, i);
	case ASTRET: {
		idx_t_ expr = ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			if (ctx.fnret.kind != TYPE_UNSET)
				err("analyzer: Missing return value");
			return i + 1;
		} else if (ctx.fnret.kind == TYPE_UNSET)
			err("analyzer: Function has no return value");

		idx_t_ ni = analyzeexpr(ctx, scps, types, ast, toks, ast.kids[i].rhs);
		if (!typecompat(ctx.fnret, types[ast.kids[i].rhs]))
			err("analyzer: Return type mismatch");
		return ni;
	}
	default:
		__builtin_unreachable();
	}
}

idx_t_
analyzeexpr(struct azctx ctx, struct scope *scps, struct type *types,
            struct ast ast, struct lexemes toks, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTNUMLIT:
		types[i].kind = TYPE_NUM;
		types[i].size = 0;
		types[i].issigned = true;
		return i + 1;
	case ASTIDENT: {
		struct strview sv = toks.strs[ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx.decl) && ctx.si > 0)
			ctx.si--;

		for (idx_t_ lvl = ctx.si;;) {
			struct scope scp = scps[lvl];
			idx_t_ *ip = symtab_insert(&scp.map, sv, NULL);

			if (ip == NULL) {
				if (lvl == 0)
					break;
				lvl = scp.up;
			} else {
				switch (types[*ip].kind) {
				case TYPE_UNSET:
					ctx.si = lvl;
					analyzedecl(ctx, scps, types, ast, toks, *ip);
					break;
				case TYPE_CHECKING:
					err("analyzer: Circular definition of ‘%.*s’", SV_PRI_ARGS(sv));
				}

				types[i] = types[*ip];
				return i + 1;
			}
		}

		err("analyzer: Unknown symbol ‘%.*s’", SV_PRI_ARGS(sv));
	}
	case ASTFN:
		return analyzefn(ctx, scps, types, ast, toks, i);
	default:
		__builtin_unreachable();
	}
}

idx_t_
analyzefn(struct azctx ctx, struct scope *scps, struct type *types,
          struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct type t = {.kind = TYPE_FN};
	struct pair p = ast.kids[i];

	idx_t_ proto = p.lhs;
	if (ast.kids[proto].rhs != AST_EMPTY) {
		t.ret = typegrab(ast, toks, ast.kids[proto].rhs);
		ctx.fnret = *t.ret;
		ctx.chkrets = true;
	} else
		ctx.fnret.kind = TYPE_UNSET;
	types[i] = t;
	return analyzeblk(ctx, scps, types, ast, toks, p.rhs);
}

idx_t_
analyzeblk(struct azctx ctx, struct scope *scps, struct type *types,
           struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct pair p = ast.kids[i];

	while (scps[ctx.si].i != p.lhs)
		ctx.si++;

	bool chkrets = ctx.chkrets, hasret = false;
	ctx.chkrets = false;

	for (i = p.lhs; i <= p.rhs;) {
		if (chkrets && returns(ast, i))
			hasret = true;
		i = analyzestmt(ctx, scps, types, ast, toks, i);
	}
	if (chkrets && !hasret)
		err("analyzer: Function doesn’t return on all paths");

	return i;
}

static idx_t_
constfolddecl(struct cfctx ctx, mpq_t *folds, struct scope *scps,
              struct type *types, struct ast ast, struct lexemes toks, idx_t_ i);
static idx_t_
constfoldexpr(struct cfctx ctx, mpq_t *folds, struct scope *scps,
              struct type *types, struct ast ast, struct lexemes toks, idx_t_ i);

idx_t_
constfoldstmt(struct cfctx ctx, mpq_t *folds, struct scope *scps,
              struct type *types, struct ast ast, struct lexemes toks, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
	case ASTPCDECL:
	case ASTPDECL:
		return constfolddecl(ctx, folds, scps, types, ast, toks, i);
	case ASTRET:
		return constfoldexpr(ctx, folds, scps, types, ast, toks,
		                     ast.kids[i].rhs);
	default:
		__builtin_unreachable();
	}
}

idx_t_
constfoldblk(struct cfctx ctx, mpq_t *folds, struct scope *scps,
             struct type *types, struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct pair p = ast.kids[i];
	while (scps[ctx.si].i != p.lhs)
		ctx.si++;
	for (i = p.lhs; i <= p.rhs;
	     i = constfoldstmt(ctx, folds, scps, types, ast, toks, i))
		;

	return i;
}

idx_t_
constfoldexpr(struct cfctx ctx, mpq_t *folds, struct scope *scps,
              struct type *types, struct ast ast, struct lexemes toks, idx_t_ i)
{
	/* Check if this expression has already been constant folded.  This
	   works because when an mpq_t is initialized via mpq_init(), it is
	   set to 0/1 meaning that the denominator pointer can’t be NULL. */
	if ((*folds[i])._mp_den._mp_d != NULL)
		return fwdnode(ast, i);

	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		mpq_init(folds[i]);

		/* TODO: Temporary allocator */
		struct strview sv = toks.strs[ast.lexemes[i]];
		char *buf = bufalloc(NULL, sv.len + 1, 1);
		size_t len = 0;

		for (size_t i = 0; i < sv.len; i++) {
			if (isdigit(sv.p[i]))
				buf[len++] = sv.p[i];
		}
		buf[len] = 0;

		(void)mpq_set_str(folds[i], buf, 10);

		free(buf);
		return fwdnode(ast, i);
	}
	case ASTIDENT: {
		struct strview sv = toks.strs[ast.lexemes[i]];

		/* Variable shadowing */
		if (strview_eq(sv, ctx.decl) && ctx.si > 0)
			ctx.si--;

		for (idx_t_ lvl = ctx.si;;) {
			struct scope scp = scps[lvl];
			idx_t_ *ip = symtab_insert(&scp.map, sv, NULL);

			if (ip == NULL) {
				assert(lvl != 0);
				lvl = scp.up;
			} else {
				switch (ast.kinds[*ip]) {
				case ASTDECL:
				case ASTPDECL:
					break;
				case ASTCDECL:
				case ASTPCDECL: {
					*folds[i] = *folds[*ip];
					if ((*folds[i])._mp_den._mp_d == NULL) {
						ctx.si = lvl;
						(void)constfolddecl(ctx, folds, scps, types, ast, toks,
						                    *ip);
						*folds[i] = *folds[*ip];
						assert((*folds[i])._mp_den._mp_d != NULL);
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

idx_t_
constfolddecl(struct cfctx ctx, mpq_t *folds, struct scope *scps,
              struct type *types, struct ast ast, struct lexemes toks, idx_t_ i)
{
	if (ast.kids[i].rhs == AST_EMPTY)
		return fwdnode(ast, i);
	ctx.decl = toks.strs[ast.lexemes[i]];
	return constfoldexpr(ctx, folds, scps, types, ast, toks, ast.kids[i].rhs);
}

void
constfold(mpq_t *folds, struct scope *scps, struct type *types, struct ast ast,
          struct lexemes toks, arena *a)
{
	struct cfctx ctx = {.a = a};
	for (idx_t_ i = 0; likely(i < ast.len);) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		i = constfolddecl(ctx, folds, scps, types, ast, toks, i);
	}
}

bool
returns(struct ast ast, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTPDECL:
	case ASTCDECL:
	case ASTPCDECL:
		return false;
	case ASTRET:
		return true;
	}
	__builtin_unreachable();
}

bool
typecompat(struct type lhs, struct type rhs)
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

idx_t_ *
symtab_insert(symtab **m, struct strview k, arena *a)
{
	for (uint64_t h = strview_hash(k); *m; h <<= 2) {
		if (strview_eq(k, (*m)->key))
			return &(*m)->val;
		m = &(*m)->child[h >> 62];
	}
	if (a == NULL)
		return NULL;
	*m = arena_new(a, symtab, 1);
	(*m)->key = k;
	return &(*m)->val;
}
