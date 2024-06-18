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
	struct declaration {
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
static void typecheckast(struct environs, struct type *, struct ast,
                         struct lexemes)
	__attribute__((nonnull));
static idx_t_ typecheckdecl(struct environs, struct type *, struct ast,
                            struct lexemes, idx_t_, idx_t_)
	__attribute__((nonnull));
static idx_t_ typecheckstmt(struct environs, struct type *, struct ast,
                            struct lexemes, idx_t_, idx_t_)
	__attribute__((nonnull));
static idx_t_ typecheckexpr(struct environs, struct type *, struct ast,
                            struct lexemes, idx_t_, idx_t_)
	__attribute__((nonnull));
static idx_t_ typecheckfn(struct environs, struct type *, struct ast,
                          struct lexemes, idx_t_, idx_t_)
	__attribute__((nonnull));
static idx_t_ typecheckblk(struct environs, struct type *, struct ast,
                           struct lexemes, idx_t_, idx_t_)
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
	struct environs evs = {0};
	struct type *types = bufalloc(NULL, ast.len, sizeof(*types));
	memset(types, 0, ast.len * sizeof(*types));

	create_environments(ast, toks, &evs, 0, ast.len - 1, &a);
	typecheckast(evs, types, ast, toks);

	arena_free(&a);
	free(evs.buf);

	return types;
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
	ev->buf = arena_new(a, struct declaration, ev->cap);

	for (idx_t_ i = beg; likely(i <= end); i++) {
		switch (ast.kinds[i]) {
		case ASTDECL:
			if (beg != 0)
				break;
			__attribute__((fallthrough));
		case ASTCDECL: {
			struct declaration cd = {.astidx = i};
			struct strview sv = toks.strs[ast.lexemes[i]];

			/* TODO: Sorted insert and existence check */
			for (size_t i = 0; i < ev->len; i++) {
				struct strview sv2 = toks.strs[ast.lexemes[ev->buf[i].astidx]];
				if (sv.len == sv2.len && memcmp(sv.p, sv2.p, sv.len) == 0) {
					err("analyzer: Symbol ‘%.*s’ declared multiple times",
					    (int)sv.len, sv.p);
				}
			}

			if (ev->len == ev->cap) {
				ev->buf = arena_grow(a, ev->buf, struct declaration, ev->cap,
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
typecheckast(struct environs evs, struct type *types, struct ast ast,
             struct lexemes toks)
{
	for (idx_t_ i = 0; likely(i < ast.len);) {
		assert(ast.kinds[i] == ASTDECL || ast.kinds[i] == ASTCDECL);
		if (types[i].kind == TYPE_UNSET)
			i = typecheckdecl(evs, types, ast, toks, 0, i);
		else {
			while (++i < ast.len && ast.kinds[i] != ASTDECL && ast.kinds[i] != ASTCDECL)
				;
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

idx_t_
typecheckdecl(struct environs evs, struct type *types, struct ast ast,
              struct lexemes toks, idx_t_ evi, idx_t_ i)
{
	types[i].kind = TYPE_CHECKING;

	struct pair p = ast.kids[i];
	struct type ltype, rtype;
	ltype.kind = TYPE_UNSET;

	assert(p.rhs != AST_EMPTY);

	if (p.lhs != AST_EMPTY)
		ltype = *typegrab(ast, toks, p.lhs);
	idx_t_ ni = typecheckexpr(evs, types, ast, toks, evi, p.rhs);
	rtype = types[p.rhs];

	if (ltype.kind == TYPE_UNSET)
		ltype = rtype;
	else if (!typecompat(ltype, rtype))
		err("analyzer: Type mismatch");

	types[i] = ltype;
	return ni;
}

idx_t_
typecheckstmt(struct environs evs, struct type *types, struct ast ast,
              struct lexemes toks, idx_t_ evi, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return typecheckdecl(evs, types, ast, toks, evi, i);
	case ASTRET: {
		idx_t_ ni = typecheckexpr(evs, types, ast, toks, evi, ast.kids[i].rhs);
		types[i] = types[ast.kids[i].rhs];
		return ni;
	}
	}

	assert(!"unreachable");
	__builtin_unreachable();
}

idx_t_
typecheckexpr(struct environs evs, struct type *types, struct ast ast,
              struct lexemes toks, idx_t_ evi, idx_t_ i)
{
	switch (ast.kinds[i]) {
	case ASTNUMLIT:
		types[i].kind = TYPE_NUM;
		types[i].size = 0;
		types[i].issigned = true;
		return i + 1;
	case ASTIDENT: {
		struct environ ev = evs.buf[evi];
		struct strview sv = toks.strs[ast.lexemes[i]];

		for (;;) {
			for (size_t j = 0; j < ev.len; j++) {
				struct strview sv2 = toks.strs[ast.lexemes[ev.buf[j].astidx]];
				if (sv.len != sv2.len || memcmp(sv.p, sv2.p, sv.len) != 0)
					continue;
				switch (types[j].kind) {
				case TYPE_UNSET:
					typecheckdecl(evs, types, ast, toks, evi, j);
					break;
				case TYPE_CHECKING:
					err("analyzer: Circular definition of ‘%.*s’", (int)sv2.len,
					    sv2.p);
				}
				types[i] = types[j];
				return i + 1;
			}
			if (evi == 0)
				err("analyzer: Unknown constant ‘%.*s’", (int)sv.len, sv.p);
			evi = ev.up;
		}
	}
	case ASTFN:
		return typecheckfn(evs, types, ast, toks, evi, i);
	default:
		err("analyzer: Unexpected AST kind %u", ast.kinds[i]);
		__builtin_unreachable();
	}
}

idx_t_
typecheckfn(struct environs evs, struct type *types, struct ast ast,
            struct lexemes toks, idx_t_ evi, idx_t_ i)
{
	struct type t = {.kind = TYPE_FN};
	struct pair p = ast.kids[i];

	idx_t_ proto = p.lhs;
	if (ast.kids[proto].rhs != AST_EMPTY)
		t.ret = typegrab(ast, toks, ast.kids[proto].rhs);
	types[i] = t;
	idx_t_ ni = typecheckblk(evs, types, ast, toks, evi, p.rhs);
	// if (!typecompat(types[i], types[p.rhs]))
	// 	err("analyzer: Type mismatch");
	return ni;
}

idx_t_
typecheckblk(struct environs evs, struct type *types, struct ast ast,
             struct lexemes toks, idx_t_ evi, idx_t_ i)
{
	struct pair p = ast.kids[i];
	for (i = p.lhs; i <= p.rhs;)
		i = typecheckstmt(evs, types, ast, toks, evi, i);
	return i;
}

bool
typecompat(struct type lhs, struct type rhs)
{
	if (rhs.kind == TYPE_FN)
		return lhs.ret == rhs.ret;

	if (lhs.kind == rhs.kind)
		return true;

	/* TODO: Need to actually parse it!  256 should not coerce to i8. */
	if (rhs.kind == TYPE_NUM)
		return true;

	if (lhs.issigned != rhs.issigned)
		return false;

	return lhs.size >= rhs.size;
}
