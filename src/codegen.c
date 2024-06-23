#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
	arena_t   *a;
	scratch_t *s;

	mpq_t     *folds;
	scope_t   *scps;
	type_t    *types;
	ast_t     ast;
	aux_t     aux;
	lexemes_t toks;

	LLVMContextRef ctx;
	LLVMModuleRef  mod;
	LLVMBuilderRef bob;
	LLVMValueRef   func;

	strview_t namespace;
};

static LLVMTypeRef type2llvm(struct cgctx, type_t);

static void codegenast(struct cgctx);

void
codegen(const char *file, mpq_t *folds, scope_t *scps, type_t *types,
        ast_t ast, aux_t aux, lexemes_t toks)
{
	char *triple = LLVMGetDefaultTargetTriple();
	LLVMContextRef llctx = LLVMContextCreate();

	struct cgctx ctx = {
		.a = &(arena_t){0},
		.s = &(scratch_t){0},

		.folds = folds,
		.scps  = scps,
		.types = types,
		.ast   = ast,
		.aux   = aux,
		.toks  = toks,

		.ctx = llctx,
		.mod = LLVMModuleCreateWithNameInContext("oryx", llctx),
		.bob = LLVMCreateBuilderInContext(llctx),
	};

	LLVMSetSourceFileName(ctx.mod, file, strlen(file));
	LLVMSetTarget(ctx.mod, triple);
	LLVMDisposeMessage(triple);

	codegenast(ctx);

	char *error = NULL;
	if (LLVMVerifyModule(ctx.mod, LLVMReturnStatusAction, &error) == 1)
		err("codegen: %s", error);

	tmpfree(ctx.s);
	arena_free(ctx.a);
	LLVMDisposeBuilder(ctx.bob);
	LLVMDisposeMessage(error);
	LLVMDumpModule(ctx.mod);
	LLVMDisposeModule(ctx.mod);
	LLVMContextDispose(ctx.ctx);
}

static idx_t codegendecl(struct cgctx ctx, idx_t);

idx_t
codegentypedexpr(struct cgctx ctx, idx_t i, type_t type, LLVMValueRef *outv)
{
	/* If true, implies numeric constant */
	if (MPQ_IS_INIT(ctx.folds[i])) {
		mpz_ptr num, den;
		num = mpq_numref(ctx.folds[i]);
		den = mpq_denref(ctx.folds[i]);
		if (mpz_cmp_ui(den, 1) != 0)
			err("Invalid integer");

		int cmp;
		if ((sizeof(unsigned long) >= 8 && type.size <= 8)
			|| type.size <= 4)
		{
			unsigned long x = 1UL << (type.size * 8 - type.issigned);
			cmp = mpz_cmp_ui(num, x - 1);
		} else {
			mpz_t x;
			mp_bitcnt_t bits = type.size * 8;
			if (type.issigned)
				bits--;
			mpz_init_set_ui(x, 1);
			mpz_mul_2exp(x, x, bits);
			mpz_sub_ui(x, x, 1);
			cmp = mpz_cmp(num, x);
			mpz_clear(x);
		}

		if (cmp > 0)
			err("Integer too large for datatype");

		/* The max value of a u128 is length 39 */
		char buf[40];
		mpz_get_str(buf, 10, num);
		*outv = LLVMConstIntOfString(type2llvm(ctx, type), buf, 10);
		return fwdnode(ctx.ast, i);
	}

	assert(ctx.ast.kinds[i] == ASTIDENT);
	err("%s():%d: not implemented", __func__, __LINE__);
}

idx_t
codegenstmt(struct cgctx ctx, idx_t i)
{
	switch (ctx.ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return codegendecl(ctx, i);
	case ASTRET: {
		idx_t expr = ctx.ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			LLVMBuildRetVoid(ctx.bob);
			return fwdnode(ctx.ast, i);
		}

		LLVMValueRef v;
		i = codegentypedexpr(ctx, expr, ctx.types[i], &v);
		(void)LLVMBuildRet(ctx.bob, v);
		return i;
	}
	default:
		__builtin_unreachable();
	}
}

idx_t
codegenblk(struct cgctx ctx, idx_t i)
{
	pair_t p = ctx.ast.kids[i];
	for (i = p.lhs; i <= p.rhs; i = codegenstmt(ctx, i))
		;
	return i;
}

idx_t
codegenfunc(struct cgctx ctx, idx_t i, const char *name)
{
	LLVMTypeRef ret = ctx.types[i].ret == NULL
	                    ? LLVMVoidTypeInContext(ctx.ctx)
	                    : type2llvm(ctx, *ctx.types[i].ret);

	LLVMTypeRef ft = LLVMFunctionType(ret, NULL, 0, false);
	ctx.func = LLVMAddFunction(ctx.mod, name, ft);
	LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx.ctx, ctx.func,
	                                                        "entry");
	LLVMPositionBuilderAtEnd(ctx.bob, entry);

	pair_t p = ctx.ast.kids[i];
	i = codegenblk(ctx, p.rhs);
	if (ctx.ast.kids[p.lhs].rhs == AST_EMPTY)
		LLVMBuildRetVoid(ctx.bob);
	return i;
}

idx_t
codegendecl(struct cgctx ctx, idx_t i)
{
	pair_t p = ctx.ast.kids[i];

	if (ctx.ast.kinds[i] == ASTCDECL) {
		/* Constants are purely a compiler concept; they aren’t generated
		   into anything */
		if (ctx.ast.kinds[p.rhs] != ASTFN)
			return fwdnode(ctx.ast, i);

		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		svtocstr(name, sv);
		return codegenfunc(ctx, p.rhs, name);
	}

	assert(ctx.ast.kinds[i] == ASTDECL);

	if (!ctx.types[i].isfloat && ctx.aux.buf[p.lhs].decl.isstatic) {
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
		LLVMValueRef globl = LLVMAddGlobal(ctx.mod, t, svtocstr(name, sv));

		LLVMValueRef v;
		i = codegentypedexpr(ctx, p.rhs, ctx.types[i], &v);
		LLVMSetInitializer(globl, v);
		LLVMSetLinkage(globl, LLVMInternalLinkage);
		return i;
	}
	if (!ctx.types[i].isfloat /* && !aux.buf[p.lhs].decl.isstatic */) {
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
		LLVMValueRef var, val;
		var = LLVMBuildAlloca(ctx.bob, t, svtocstr(name, sv));
		i = codegentypedexpr(ctx, p.rhs, ctx.types[i], &val);
		LLVMBuildStore(ctx.bob, val, var);
		return i;
	}

	/* types[i].isfloat */
	err("%s():%d: TODO", __func__, __LINE__);
}

void
codegenast(struct cgctx ctx)
{
	for (idx_t i = 0; i < ctx.ast.len; i = codegendecl(ctx, i))
		;
}

LLVMTypeRef
type2llvm(struct cgctx ctx, type_t t)
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
