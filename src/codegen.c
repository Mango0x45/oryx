#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gmp.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "alloc.h"
#include "codegen.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "types.h"

typedef mpq_t constval;

typedef struct constmap {
    struct constmap *child[4];
    struct strview key;
    constval val;
} constmap;

/* A context structure we can pass to all the codegen functions just so they
   have easy access to everything */
struct cgctx {
	LLVMContextRef ctx;
	LLVMModuleRef mod;
	LLVMBuilderRef bob;
	struct strview namespace;
	constmap *consts;
	arena a;
};

static void codegenprog(struct cgctx, struct type *, struct ast,
                        struct lexemes)
	__attribute__((nonnull));
// static size_t codegenexpr(struct cgctx, struct type *, struct ast,
//                           struct lexemes, size_t, LLVMValueRef *)
// 	__attribute__((nonnull));

static LLVMTypeRef type2llvm(struct type);

static bool constmap_equals(struct strview, struct strview);
static uint64_t constmap_hash(struct strview);
static constval *constmap_upsert(constmap **, struct strview, arena *)
	__attribute__((nonnull(1)));

/* TODO: Don’t do this? */
#define lengthof(xs) (sizeof(xs) / sizeof(*(xs)))

void
codegen(const char *file, struct type *types, struct ast ast,
        struct lexemes toks)
{
	struct cgctx ctx = {0};
	ctx.ctx = LLVMContextCreate();
	ctx.mod = LLVMModuleCreateWithNameInContext("oryx", ctx.ctx);
	ctx.bob = LLVMCreateBuilderInContext(ctx.ctx);
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));

	codegenprog(ctx, types, ast, toks);

	arena_free(&ctx.a);

	LLVMDisposeBuilder(ctx.bob);

	char *error = NULL;
	LLVMVerifyModule(ctx.mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMDumpModule(ctx.mod);
	LLVMDisposeModule(ctx.mod);
	LLVMContextDispose(ctx.ctx);
}

void
interp_const_expr(struct cgctx ctx, struct type *types, struct ast ast,
                  struct lexemes toks, idx_t_ i, constval v)
{
	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		mpf_t f;
		mpq_init(v);

		struct strview sv = toks.strs[ast.lexemes[i]];
		char *buf = bufalloc(NULL, sv.len + 1, 1);
		memcpy(buf, sv.p, sv.len);
		buf[sv.len] = 0;

		if (mpf_init_set_str(f, buf, 10) == -1)
			err("mpf_init_set_str: Invalid input ‘%s’", buf);
		free(buf);

		mpq_set_f(v, f);
		mpf_clear(f);
		break;
	}
	case ASTIDENT: {
		struct strview sv = toks.strs[ast.lexemes[i]];
		constval *w = constmap_upsert(&ctx.consts, sv, NULL);
		v = *w;
		break;
	}
	default:
		err("codegen: Not implemented");
	}
}

void
register_const(struct cgctx ctx, struct type *types, struct ast ast,
               struct lexemes toks, idx_t_ i)
{
	struct pair p = ast.kids[i];
	assert(p.rhs != AST_EMPTY);

	constval v;
	interp_const_expr(ctx, types, ast, toks, p.rhs, v);

	struct strview sv = toks.strs[ast.lexemes[i]];
	*constmap_upsert(&ctx.consts, sv, &ctx.a) = v;

	/* TODO: Remove */
	printf("%.*s = ", (int)sv.len, sv.p);
	mpq_out_str(stdout, 10, v);
	putchar('\n');
	mpq_clear(v);
}

void
codegenprog(struct cgctx ctx, struct type *types, struct ast ast,
            struct lexemes toks)
{
	for (size_t i = 0; i < ast.len; i = fwdnode(ast, i)) {
		if ((ast.kinds[i] | 1) == ASTPCDECL)
			register_const(ctx, types, ast, toks, i);

		// mpq_t v = interp_const_expr(ctx, ast, toks, p.rhs);
		// constmap_upsert(&ctx.consts, ast.lexemes[p.lhs], &ctx.a);
	}

	// for (size_t i = 0; i < ast.len;) {
	// 	assert(ast.kinds[i] <= _AST_DECLS_END);
	// 	i = codegenprog(ctx, types, ast, toks, i);
	// }
}

// size_t
// codegenexpr(struct cgctx ctx, struct type *types, struct ast ast,
//             struct lexemes toks, size_t i, LLVMValueRef *v)
// {
// 	(void)ctx;
// 	switch (ast.kinds[i]) {
// 	case ASTNUMLIT: {
// 		/* TODO: Arbitrary precision? */
// 		struct strview sv = toks.strs[ast.lexemes[i]];
//
// 		bool has_sep = memchr(sv.p, '\'', sv.len) != NULL;
//
// 		/* TODO: Temporary one-time-use allocator? */
// 		if (has_sep) {
// 			size_t len = 0;
// 			char *p = bufalloc(NULL, sv.len, 1);
// 			for (size_t i = 0; i < sv.len; i++) {
// 				if (sv.p[i] != '\'')
// 					p[len++] = sv.p[i];
// 			}
//
// 			*v = LLVMConstIntOfStringAndSize(type2llvm(types[i]), p, len, 10);
// 			free(p);
// 		} else {
// 			*v = LLVMConstIntOfStringAndSize(type2llvm(types[i]), sv.p, sv.len,
// 			                                 10);
// 		}
// 		return i + 1;
// 	}
// 	case ASTIDENT:
// 		err("codegen: %s: Not implemented", __func__);
// 	default:
// 		assert(!"unreachable");
// 		__builtin_unreachable();
// 	}
// }

LLVMTypeRef
type2llvm(struct type t)
{
	switch (t.kind) {
	case TYPE_UNSET:
	case TYPE_CHECKING:
		assert(!"codegen: Hit TYPE_UNSET or TYPE_CHECKING");
		__builtin_unreachable();
	case TYPE_FN:
		err("codegen: %s: Not implemented", __func__);
	case TYPE_NUM:
		/* TODO: Floats */
		/* TODO: Arbitrary precision */
		if (t.size == 0)
			return LLVMInt64Type();
		return LLVMIntType((unsigned)t.size * 8);
	default:
		__builtin_unreachable();
	}
}

bool
constmap_equals(struct strview x, struct strview y)
{
	return x.len == y.len && memcmp(x.p, y.p, x.len) == 0;
}

uint64_t
constmap_hash(struct strview sv)
{
	uint64_t h = 0x100;
	for (size_t i = 0; i < sv.len; i++) {
		h ^= sv.p[i];
		h *= 1111111111111111111u;
	}
	return h;
}

constval *
constmap_upsert(constmap **m, struct strview key, arena *a)
{
	for (uint64_t h = constmap_hash(key); *m; h <<= 2) {
		if (constmap_equals(key, (*m)->key))
			return &(*m)->val;
		m = &(*m)->child[h >> 62];
	}
	if (a == NULL)
		return NULL;
	*m = arena_new(a, constmap, 1);
	(*m)->key = key;
	return &(*m)->val;
}
