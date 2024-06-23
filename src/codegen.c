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
	arena_t *a;
	scratch_t *s;
	LLVMContextRef ctx;
	LLVMModuleRef mod;
	LLVMBuilderRef bob;
	LLVMValueRef func;
	strview_t namespace;
};

static LLVMTypeRef type2llvm(struct cgctx, type_t);

static void codegenast(struct cgctx, mpq_t *, type_t *, ast_t, aux_t ,
                       lexemes_t)
	__attribute__((nonnull));

void
codegen(const char *file, mpq_t *folds, scope_t *scps, type_t *types,
        ast_t ast, aux_t aux, lexemes_t toks)
{
	(void)scps;
	char *triple = LLVMGetDefaultTargetTriple();

	struct cgctx ctx;
	ctx.a = &(arena_t){0};
	ctx.s = &(scratch_t){0};
	ctx.namespace.p = NULL;
	ctx.ctx = LLVMContextCreate();
	ctx.mod = LLVMModuleCreateWithNameInContext("oryx", ctx.ctx);
	ctx.bob = LLVMCreateBuilderInContext(ctx.ctx);
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));
	LLVMSetTarget(ctx.mod, triple);
	LLVMDisposeMessage(triple);

	codegenast(ctx, folds, types, ast, aux, toks);

	arena_free(ctx.a);
	tmpfree(ctx.s);

	LLVMDisposeBuilder(ctx.bob);

	char *error = NULL;
	if (LLVMVerifyModule(ctx.mod, LLVMReturnStatusAction, &error) == 1)
		err("codegen: %s", error);

	LLVMDisposeMessage(error);
	LLVMDumpModule(ctx.mod);
	LLVMDisposeModule(ctx.mod);
	LLVMContextDispose(ctx.ctx);
}

static idx_t
codegendecl(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i);

idx_t
codegentypedexpr(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast,
                 aux_t aux, lexemes_t toks, idx_t i, type_t type,
                 LLVMValueRef *outv)
{
	/* If true, implies numeric constant */
	if (MPQ_IS_INIT(folds[i])) {
		mpz_ptr num, den;
		num = mpq_numref(folds[i]);
		den = mpq_denref(folds[i]);
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
		return fwdnode(ast, i);
	}

	assert(ast.kinds[i] == ASTIDENT);
	err("%s():%d: not implemented", __func__, __LINE__);
}

idx_t
codegenstmt(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i)
{
	switch (ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return codegendecl(ctx, folds, types, ast, aux, toks, i);
	case ASTRET: {
		idx_t expr = ast.kids[i].rhs;
		if (expr == AST_EMPTY) {
			LLVMBuildRetVoid(ctx.bob);
			return fwdnode(ast, i);
		}

		LLVMValueRef v;
		i = codegentypedexpr(ctx, folds, types, ast, aux,
		                     toks, expr, types[i], &v);
		(void)LLVMBuildRet(ctx.bob, v);
		return i;
	}
	default:
		__builtin_unreachable();
	}
}

idx_t
codegenblk(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast, aux_t aux,
           lexemes_t toks, idx_t i)
{
	pair_t p = ast.kids[i];
	for (i = p.lhs; i <= p.rhs;
	     i = codegenstmt(ctx, folds, types, ast, aux, toks, i))
		;
	return i;
}

idx_t
codegenfunc(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast,
            aux_t aux, lexemes_t toks, idx_t i, const char *name)
{
	LLVMTypeRef ret = types[i].ret == NULL
	                    ? LLVMVoidTypeInContext(ctx.ctx)
	                    : type2llvm(ctx, *types[i].ret);

	LLVMTypeRef ft = LLVMFunctionType(ret, NULL, 0, false);
	ctx.func = LLVMAddFunction(ctx.mod, name, ft);
	LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx.ctx, ctx.func,
	                                                        "entry");
	LLVMPositionBuilderAtEnd(ctx.bob, entry);

	pair_t p = ast.kids[i];
	i = codegenblk(ctx, folds, types, ast, aux, toks, p.rhs);
	if (ast.kids[p.lhs].rhs == AST_EMPTY)
		LLVMBuildRetVoid(ctx.bob);
	return i;
}

idx_t
codegendecl(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast, aux_t aux,
            lexemes_t toks, idx_t i)
{
	pair_t p = ast.kids[i];

	if (ast.kinds[i] == ASTCDECL) {
		/* Constants are purely a compiler concept; they aren’t generated
		   into anything */
		if (ast.kinds[p.rhs] != ASTFN)
			return fwdnode(ast, i);

		strview_t sv = toks.strs[ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		svtocstr(name, sv);
		return codegenfunc(ctx, folds, types, ast, aux, toks, p.rhs, name);
	}

	assert(ast.kinds[i] == ASTDECL);

	if (!types[i].isfloat && aux.buf[p.lhs].decl.isstatic) {
		strview_t sv = toks.strs[ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, types[i]);
		LLVMValueRef globl = LLVMAddGlobal(ctx.mod, t, svtocstr(name, sv));

		LLVMValueRef v;
		i = codegentypedexpr(ctx, folds, types, ast, aux, toks, p.rhs, types[i], &v);
		LLVMSetInitializer(globl, v);
		LLVMSetLinkage(globl, LLVMInternalLinkage);
		return i;
	}
	if (!types[i].isfloat /* && !aux.buf[p.lhs].decl.isstatic */) {
		strview_t sv = toks.strs[ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, types[i]);
		LLVMValueRef var, val;
		var = LLVMBuildAlloca(ctx.bob, t, svtocstr(name, sv));
		i = codegentypedexpr(ctx, folds, types, ast, aux, toks, p.rhs, types[i], &val);
		LLVMBuildStore(ctx.bob, val, var);
		return i;
	}

	/* types[i].isfloat */
	err("%s():%d: TODO", __func__, __LINE__);
}

void
codegenast(struct cgctx ctx, mpq_t *folds, type_t *types, ast_t ast, aux_t aux,
           lexemes_t toks)
{
	for (idx_t i = 0; i < ast.len;
	     i = codegendecl(ctx, folds, types, ast, aux, toks, i))
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
