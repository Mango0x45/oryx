#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>

#include "alloc.h"
#include "analyzer.h"
#include "common.h"
#include "errors.h"
#include "strview.h"

#define lengthof(xs) (sizeof(xs) / sizeof(*(xs)))

/* A context structure we can pass to all the codegen functions just so they
   have easy access to everything */
struct cgctx {
	arena a;
	LLVMContextRef ctx;
	LLVMModuleRef mod;
	LLVMBuilderRef bob;
	struct strview namespace;
};

// static LLVMTypeRef type2llvm(struct cgctx, struct type);
// static void str2val(mpq_t, struct strview);
// static struct val *cvmap_insert(cvmap **, struct strview, arena *)
// 	__attribute__((nonnull(1)));

void
codegen(const char *file, struct type *types, struct ast ast,
        struct lexemes toks)
{
	(void)types;
	(void)ast;
	(void)toks;
	char *triple = LLVMGetDefaultTargetTriple();

	struct cgctx ctx;
	ctx.a           = NULL;
	ctx.namespace.p = NULL;
	ctx.ctx         = LLVMContextCreate();
	ctx.mod         = LLVMModuleCreateWithNameInContext("oryx", ctx.ctx);
	ctx.bob         = LLVMCreateBuilderInContext(ctx.ctx);
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));
	LLVMSetTarget(ctx.mod, triple);
	LLVMDisposeMessage(triple);

	arena_free(&ctx.a);

	LLVMDisposeBuilder(ctx.bob);

	char *error = NULL;
	if (LLVMVerifyModule(ctx.mod, LLVMReturnStatusAction, &error) == 1)
		err("codegen: %s", error);

	LLVMDisposeMessage(error);
	LLVMDumpModule(ctx.mod);
	LLVMDisposeModule(ctx.mod);
	LLVMContextDispose(ctx.ctx);
}

// LLVMTypeRef
// type2llvm(struct cgctx ctx, struct type t)
// {
// 	switch (t.kind) {
// 	case TYPE_FN:
// 		err("codegen: %s: Not implemented for function types", __func__);
// 	case TYPE_NUM:
// 		/* TODO: Floats */
// 		if (t.isfloat)
// 			err("codegen: %s: Not implemented for floats", __func__);
// 		/* TODO: Arbitrary precision */
// 		if (t.size == 0)
// 			return LLVMInt64TypeInContext(ctx.ctx);
// 		assert((unsigned)t.size * 8 <= UINT8_MAX);
// 		return LLVMIntTypeInContext(ctx.ctx, t.size * 8);
// 	default:
// 		__builtin_unreachable();
// 	}
// }
//
// void
// str2val(mpq_t rop, struct strview sv)
// {
// 	mpq_init(rop);
// 	char *clean = bufalloc(NULL, sv.len + 1, 1);
// 	size_t len = 0;
//
// 	for (size_t i = 0; i < sv.len; i++) {
// 		if (isdigit(sv.p[i]))
// 			clean[len++] = sv.p[i];
// 	}
// 	clean[len] = 0;
//
// 	mpq_set_str(rop, clean, 10);
//
// 	free(clean);
// }
//
// struct val *
// cvmap_insert(cvmap **m, struct strview k, arena *a)
// {
// 	for (uint64_t h = strview_hash(k); *m; h <<= 2) {
// 		if (strview_eq(k, (*m)->key))
// 			return &(*m)->val;
// 		m = &(*m)->child[h >> 62];
// 	}
// 	if (a == NULL)
// 		return NULL;
// 	*m = arena_new(a, cvmap, 1);
// 	(*m)->key = k;
// 	return &(*m)->val;
// }
