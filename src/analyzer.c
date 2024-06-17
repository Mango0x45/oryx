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
	idx_t_ up;
	struct constdecl {
		struct type type;
		idx_t_ astidx;
	} *buf;
	size_t len, cap;
};

struct environs {
	struct environ *buf;
	size_t len, cap;
};

static struct environ *create_environments(struct ast, struct lexemes,
                                           struct environs *, idx_t_, idx_t_,
                                           arena *)
	__attribute__((returns_nonnull, nonnull));
static void typecheck_environment(struct environs, struct ast, struct lexemes,
                                  idx_t_);
static struct type typecheck_constdecl(struct environs, struct ast,
                                       struct lexemes, idx_t_, idx_t_);
static struct type typecheck_expr(struct environs, struct ast, struct lexemes,
                                  idx_t_, idx_t_);
static struct type typecheck_fn(struct environs, struct ast, struct lexemes,
                                idx_t_, idx_t_);
static const struct type *typegrab(struct ast, struct lexemes, idx_t_)
	__attribute__((returns_nonnull));
static bool typecompat(struct type, struct type);

/* Defined in primitives.gperf */
const struct type *typelookup(const uchar *, size_t)
	__attribute__((nonnull));

void
analyzeast(struct ast ast, struct lexemes toks)
{
	arena a = NULL;
	struct environs evs = {0};

	create_environments(ast, toks, &evs, 0, ast.len - 1, &a);
	for (size_t i = 0; i < evs.len; i++)
		typecheck_environment(evs, ast, toks, i);

	arena_free(&a);
	free(evs.buf);
}

struct environ *
create_environments(struct ast ast, struct lexemes toks, struct environs *evs,
                    idx_t_ beg, idx_t_ end, arena *a)
{
	assert(evs != NULL);

	if (evs->len == evs->cap) {
		evs->cap = evs->cap == 0 ? 8 : evs->cap * 2;
		evs->buf = bufalloc(evs->buf, evs->cap, sizeof(*evs->buf));
	}

	struct environ *ev = evs->buf + evs->len++;
	*ev = (struct environ){.cap = 16};
	ev->buf = arena_new(a, struct constdecl, ev->cap);

	for (idx_t_ i = beg; likely(i <= end); i++) {
		switch (ast.kinds[i]) {
		case ASTCDECL: {
			struct constdecl cd = {.astidx = i};
			struct strview sv = toks.strs[ast.lexemes[i]];

			/* TODO: Sorted insert and existence check */
			for (size_t i = 0; i < ev->len; i++) {
				struct strview sv2 = toks.strs[ast.lexemes[ev->buf[i].astidx]];
				if (sv.len == sv2.len && memcmp(sv.p, sv2.p, sv.len) == 0) {
					err("analyzer: Constant ‘%.*s’ declared multiple times",
					    (int)sv.len, sv.p);
				}
			}

			if (ev->len == ev->cap) {
				ev->buf = arena_grow(a, ev->buf, struct constdecl, ev->cap,
				                     ev->cap * 2);
				ev->cap *= 2;
			}
			ev->buf[ev->len++] = cd;
			break;
		}
		case ASTBLK: {
			idx_t_ lhs, rhs, up;
			lhs = ast.kids[i].lhs;
			rhs = ast.kids[i].rhs;
			up = ev - evs->buf;
			create_environments(ast, toks, evs, lhs, rhs, a)->up = up;
			i = rhs;
			break;
		}
		}
	}

	return ev;
}

void
typecheck_environment(struct environs evs, struct ast ast, struct lexemes toks,
                      idx_t_ i)
{
	struct environ ev = evs.buf[i];
	for (size_t j = 0; j < ev.len; j++)
		typecheck_constdecl(evs, ast, toks, j, i);
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

struct type
typecheck_constdecl(struct environs evs, struct ast ast, struct lexemes toks,
                    idx_t_ i, idx_t_ evi)
{
	struct environ ev = evs.buf[evi];
	struct constdecl cd = ev.buf[i];
	if (cd.type.kind != TYPE_UNSET)
		return cd.type;
	ev.buf[i].type.kind = TYPE_CHECKING;

	struct pair p = ast.kids[cd.astidx];
	struct type ltype, rtype;
	ltype.kind = TYPE_UNSET;

	assert(p.rhs != AST_EMPTY);

	if (p.lhs != AST_EMPTY)
		ltype = *typegrab(ast, toks, p.lhs);
	rtype = typecheck_expr(evs, ast, toks, p.rhs, evi);

	if (ltype.kind == TYPE_UNSET)
		ltype = rtype;
	else if (!typecompat(ltype, rtype))
		err("analyzer: Type mismatch");

	return ev.buf[i].type = ltype;
}

struct type
typecheck_expr(struct environs evs, struct ast ast, struct lexemes toks,
               idx_t_ i, idx_t_ evi)
{
	switch (ast.kinds[i]) {
	case ASTNUMLIT:
		return (struct type){.kind = TYPE_INT_UNTYPED, .issigned = true};
	case ASTIDENT: {
		struct environ ev = evs.buf[evi];
		struct strview sv = toks.strs[ast.lexemes[i]];

		for (;;) {
			for (size_t i = 0; i < ev.len; i++) {
				struct strview sv2 = toks.strs[ast.lexemes[ev.buf[i].astidx]];
				if (sv.len != sv2.len || memcmp(sv.p, sv2.p, sv.len) != 0)
					continue;
				struct type t = typecheck_constdecl(evs, ast, toks, i, evi);
				if (t.kind == TYPE_CHECKING) {
					err("analyzer: Circular dependency for type ‘%.*s’",
					    (int)sv2.len, sv2.p);
				}
				return t;
			}
			if (evi == 0)
				err("analyzer: Unknown constant ‘%.*s’", (int)sv.len, sv.p);
			evi = ev.up;
		}
	}
	case ASTFN:
		return typecheck_fn(evs, ast, toks, i, evi);
	default:
		err("analyzer: Unexpected AST kind %u", ast.kinds[i]);
		__builtin_unreachable();
	}
}

struct type
typecheck_fn(struct environs evs, struct ast ast, struct lexemes toks,
             idx_t_ i, idx_t_ evi)
{
	struct type t = {.kind = TYPE_FN};
	struct pair p = ast.kids[i];

	idx_t_ proto = p.lhs;
	if (ast.kids[proto].rhs == AST_EMPTY)
		return t;

	t.ret = typegrab(ast, toks, ast.kids[proto].rhs);
	return t;

	/* TODO: Typecheck function body */
}

bool
typecompat(struct type lhs, struct type rhs)
{
	if (rhs.kind == TYPE_FN)
		return lhs.ret == rhs.ret;

	if (lhs.kind == rhs.kind)
		return true;

	/* TODO: Need to actually parse it!  256 should not coerce to i8. */
	if (rhs.kind == TYPE_INT_UNTYPED)
		return true;

	if (lhs.issigned != rhs.issigned)
		return false;

	return lhs.size >= rhs.size;
}
