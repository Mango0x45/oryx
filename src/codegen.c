#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include <gmp.h>
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

static LLVMTypeRef type2llvm(struct cgctx, struct type);
// static void str2val(mpq_t, struct strview);
// static struct val *cvmap_insert(cvmap **, struct strview, arena *)
// 	__attribute__((nonnull(1)));

static void codegenast(struct cgctx, mpq_t *, struct type *, struct ast,
                       struct lexemes)
	__attribute__((nonnull));

void
codegen(const char *file, mpq_t *folds, struct scope *scps, struct type *types,
        struct ast ast, struct lexemes toks)
{
	(void)scps;
	char *triple = LLVMGetDefaultTargetTriple();

	struct cgctx ctx;
	ctx.a = NULL;
	ctx.namespace.p = NULL;
	ctx.ctx = LLVMContextCreate();
	ctx.mod = LLVMModuleCreateWithNameInContext("oryx", ctx.ctx);
	ctx.bob = LLVMCreateBuilderInContext(ctx.ctx);
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));
	LLVMSetTarget(ctx.mod, triple);
	LLVMDisposeMessage(triple);

	codegenast(ctx, folds, types, ast, toks);

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

idx_t_
codegenfunc(struct cgctx ctx, mpq_t *folds, struct type *types, struct ast ast,
            struct lexemes toks, idx_t_ i, const char *name)
{
	LLVMTypeRef ret = type2llvm(ctx, types[ast.kids[i].rhs]);
	LLVMTypeRef ft = LLVMFunctionType(ret, NULL, 0, false);
	LLVMValueRef fn = LLVMAddFunction(ctx.mod, name, ft);
	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(fn, "entry");

	struct pair p = ast.kids[i];
	// for (i = p.lhs; i <= p.rhs; i = codegenstmt(ctx, folds, types, ast, toks, i))
	// 	;
	return fwdnode(ast, p.rhs);
}

idx_t_
codegendecl(struct cgctx ctx, mpq_t *folds, struct type *types, struct ast ast,
            struct lexemes toks, idx_t_ i)
{
	/* Constants are purely a compiler concept; they aren’t generated
	   into anything */
	if (ast.kinds[i] == ASTCDECL)
		return fwdnode(ast, i);

	struct pair p = ast.kids[i];
	if (ast.kinds[p.rhs] == ASTFN) {
		struct strview sv = toks.strs[ast.lexemes[i]];
		/* TODO: Namespace the name */
		/* TODO: Temporary allocator */
		char *name = bufalloc(NULL, sv.len + 1, 1);
		svtocstr(name, sv);
		i = codegenfunc(ctx, folds, types, ast, toks, p.rhs, name);
		free(name);
		return i;
	} else if (!types[i].isfloat) {
		struct strview sv = toks.strs[ast.lexemes[i]];
		/* TODO: Namespace the name */
		/* TODO: Temporary allocator */
		char *name = bufalloc(NULL, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, types[i]);
		LLVMValueRef globl = LLVMAddGlobal(ctx.mod, t, svtocstr(name, sv));
		free(name);

#if DEBUG /* Assert that the fold is an integer */
		mpz_t den;
		mpq_canonicalize(folds[p.rhs]);
		mpq_get_den(den, folds[p.rhs]);
		assert(mpz_cmp_si(den, 1) == 0);
		mpz_clear(den);
#endif

		/* The max value of a u128 is length 39 */
		char buf[40];
		mpq_get_str(buf, 10, folds[p.rhs]);

		LLVMSetInitializer(globl, LLVMConstIntOfString(t, buf, 10));
		LLVMSetLinkage(globl, LLVMInternalLinkage);

		return fwdnode(ast, i);
	} else /* && types[i].isfloat */
		err("%s():%d: TODO", __func__, __LINE__);
}

void
codegenast(struct cgctx ctx, mpq_t *folds, struct type *types, struct ast ast,
           struct lexemes toks)
{
	for (idx_t_ i = 0; i < ast.len;
	     i = codegendecl(ctx, folds, types, ast, toks, i))
		;
}

LLVMTypeRef
type2llvm(struct cgctx ctx, struct type t)
{
	switch (t.kind) {
	case TYPE_FN:
		err("codegen: %s: Not implemented for function types", __func__);
	case TYPE_NUM:
		if (t.isfloat) {
			switch (t.size) {
			case  2: return LLVMHalfTypeInContext(ctx.ctx);
			case  4: return LLVMFloatTypeInContext(ctx.ctx);
			case  0:
			case  8: return LLVMDoubleTypeInContext(ctx.ctx);
			case 16: return LLVMFP128TypeInContext(ctx.ctx);
			default: __builtin_unreachable();
			}
		}
		/* TODO: Arbitrary precision */
		if (t.size == 0)
			return LLVMInt64TypeInContext(ctx.ctx);
		assert((unsigned)t.size * 8 <= UINT8_MAX);
		return LLVMIntTypeInContext(ctx.ctx, t.size * 8);
	default:
		__builtin_unreachable();
	}
}

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
