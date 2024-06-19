#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "analyzer.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "types.h"

struct environ {
	struct declaration {
		idx_t_ astidx;
	} *buf;
	size_t len, cap;
};

struct evstack {
	struct environ *buf;
	size_t len, cap;
};

struct typechkctx {
	struct type fnret;
	struct strview decl;
};

static void typechkast(struct evstack, struct type *, struct ast,
                       struct lexemes)
	__attribute__((nonnull));
static idx_t_ typechkdecl(struct typechkctx, struct evstack, struct type *,
                          struct ast, struct lexemes, idx_t_)
	__attribute__((nonnull));
static idx_t_ typechkstmt(struct typechkctx, struct evstack, struct type *,
                          struct ast, struct lexemes, idx_t_)
	__attribute__((nonnull));
static idx_t_ typechkexpr(struct typechkctx, struct evstack, struct type *,
                          struct ast, struct lexemes, idx_t_)
	__attribute__((nonnull));
static idx_t_ typechkfn(struct typechkctx, struct evstack, struct type *,
                        struct ast, struct lexemes, idx_t_)
	__attribute__((nonnull));
static idx_t_ typechkblk(struct typechkctx, struct evstack, struct type *,
                         struct ast, struct lexemes, idx_t_)
	__attribute__((nonnull));

static const struct type *typegrab(struct ast, struct lexemes, idx_t_)
	__attribute__((returns_nonnull));
static bool typecompat(struct type, struct type);

/* Defined in primitives.gperf */
const struct type *typelookup(const uchar *, size_t)
	__attribute__((nonnull));

struct type *
analyzeast(struct ast ast, struct lexemes toks)
{
	arena a = NULL;
	struct evstack evs = {.cap = 16};
	evs.buf = bufalloc(NULL, evs.cap, sizeof(*evs.buf));
	struct type *types = bufalloc(NULL, ast.len, sizeof(*types));
	memset(types, 0, ast.len * sizeof(*types));

	typechkast(evs, types, ast, toks);

	arena_free(&a);
	free(evs.buf);
	return types;
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
typechkast(struct evstack evs, struct type *types, struct ast ast,
           struct lexemes toks)
{
	struct environ ev = {.cap = 16};
	ev.buf = bufalloc(NULL, ev.cap, sizeof(*ev.buf));

	for (idx_t_ i = 0; likely(i < ast.len); i = fwdnode(ast, i)) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		if (ev.len == ev.cap) {
			ev.cap *= 2;
			ev.buf = bufalloc(ev.buf, ev.cap, sizeof(*ev.buf));
		}
		ev.buf[ev.len++] = (struct declaration){.astidx = i};
	}

	assert(evs.cap > 0);
	evs.buf[0] = ev;
	evs.len = 1;

	struct typechkctx ctx = {0};
	for (idx_t_ i = 0; likely(i < ast.len); i = fwdnode(ast, i)) {
		assert(ast.kinds[i] <= _AST_DECLS_END);
		typechkdecl(ctx, evs, types, ast, toks, i);
	}

	free(ev.buf);
}

idx_t_
typechkdecl(struct typechkctx ctx, struct evstack evs, struct type *types,
            struct ast ast, struct lexemes toks, idx_t_ i)
{
	ctx.decl = toks.strs[ast.lexemes[i]];
	types[i].kind = TYPE_CHECKING;

	struct pair p = ast.kids[i];
	struct type ltype, rtype;
	ltype.kind = TYPE_UNSET;

	assert(p.lhs != AST_EMPTY || p.rhs != AST_EMPTY);

	idx_t_ ni;

	if (p.lhs != AST_EMPTY)
		ltype = *typegrab(ast, toks, p.lhs);
	if (p.rhs != AST_EMPTY) {
		ni = typechkexpr(ctx, evs, types, ast, toks, p.rhs);
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
typechkstmt(struct typechkctx ctx, struct evstack evs, struct type *types,
            struct ast ast, struct lexemes toks, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return typechkdecl(ctx, evs, types, ast, toks, i);
	case ASTRET: {
		idx_t_ expr = ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			if (ctx.fnret.kind != TYPE_UNSET)
				err("analyzer: Missing return value");
			return i + 1;
		} else if (ctx.fnret.kind == TYPE_UNSET)
			err("analyzer: Function has no return value");

		idx_t_ ni = typechkexpr(ctx, evs, types, ast, toks, ast.kids[i].rhs);
		if (!typecompat(ctx.fnret, types[ast.kids[i].rhs]))
			err("analyzer: Return type mismatch");
		return ni;
	}
	default:
		assert(!"unreachable");
		__builtin_unreachable();
	}
}

idx_t_
typechkexpr(struct typechkctx ctx, struct evstack evs, struct type *types,
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

		for (size_t lvl = evs.len; lvl-- > 0;) {
			struct environ ev = evs.buf[lvl];
			/* TODO: Binary search */
			for (size_t j = 0; j < ev.len; j++) {
				struct strview sv2 = toks.strs[ast.lexemes[ev.buf[j].astidx]];
				if (sv.len != sv2.len || memcmp(sv.p, sv2.p, sv.len) != 0)
					continue;

				/* We need this to allow shadowing in situations like
				   FOO :FOO */
				if (lvl == evs.len - 1 && sv.len == ctx.decl.len
				    && memcmp(sv.p, ctx.decl.p, sv.len) == 0)
				{
					break;
				}

				switch (types[j].kind) {
				case TYPE_UNSET:
					evs.len = lvl + 1;
					typechkdecl(ctx, evs, types, ast, toks, ev.buf[j].astidx);
					break;
				case TYPE_CHECKING:
					err("analyzer: Circular definition of ‘%.*s’", (int)sv.len,
					    sv.p);
				}

				types[i] = types[j];
				return i + 1;
			}
		}

		err("analyzer: Unknown constant ‘%.*s’", (int)sv.len, sv.p);
	}
	case ASTFN:
		return typechkfn(ctx, evs, types, ast, toks, i);
	default:
		err("analyzer: Unexpected AST kind %u", ast.kinds[i]);
		__builtin_unreachable();
	}
}

idx_t_
typechkfn(struct typechkctx ctx, struct evstack evs, struct type *types,
          struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct type t = {.kind = TYPE_FN};
	struct pair p = ast.kids[i];

	idx_t_ proto = p.lhs;
	if (ast.kids[proto].rhs != AST_EMPTY) {
		t.ret = typegrab(ast, toks, ast.kids[proto].rhs);
		ctx.fnret = *t.ret;
	} else
		ctx.fnret.kind = TYPE_UNSET;
	types[i] = t;
	idx_t_ ni = typechkblk(ctx, evs, types, ast, toks, p.rhs);
	return ni;
}

idx_t_
typechkblk(struct typechkctx ctx, struct evstack evs, struct type *types,
           struct ast ast, struct lexemes toks, idx_t_ i)
{
	struct environ ev = {.cap = 16};
	ev.buf = bufalloc(NULL, ev.cap, sizeof(*ev.buf));

	struct pair p = ast.kids[i];

	for (idx_t_ i = p.lhs; likely(i < p.rhs); i = fwdnode(ast, i)) {
		if (ast.kinds[i] != ASTCDECL)
			continue;
		if (ev.len == ev.cap) {
			ev.cap *= 2;
			ev.buf = bufalloc(ev.buf, ev.cap, sizeof(*ev.buf));
		}
		ev.buf[ev.len++] = (struct declaration){.astidx = i};
	}

	if (evs.len == evs.cap) {
		evs.cap *= 2;
		evs.buf = bufalloc(evs.buf, evs.cap, sizeof(*evs.buf));
	}
	evs.buf[evs.len++] = ev;

	for (i = p.lhs; i <= p.rhs; i = typechkstmt(ctx, evs, types, ast, toks, i))
		;

	free(ev.buf);
	return i;
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
	   and sign */
	return lhs.issigned == rhs.issigned && lhs.size == rhs.size;
}
