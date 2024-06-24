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

/* Cheers to Lernö for this */
#define LLVM_TARGET_INIT(x)                                                    \
	do {                                                                       \
		LLVMInitialize##x##AsmParser();                                        \
		LLVMInitialize##x##AsmPrinter();                                       \
		LLVMInitialize##x##TargetInfo();                                       \
		LLVMInitialize##x##Target();                                           \
		LLVMInitialize##x##Disassembler();                                     \
		LLVMInitialize##x##TargetMC();                                         \
	} while (false)


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

	LLVMBuilderRef    bob;
	LLVMContextRef    ctx;
	LLVMModuleRef     mod;
	LLVMTargetDataRef td;
	LLVMValueRef      func;

	idx_t scpi;
	strview_t namespace;
};

static LLVMTypeRef type2llvm(struct cgctx, type_t);

static void codegenast(struct cgctx);

void
codegen(const char *file, mpq_t *folds, scope_t *scps, type_t *types, ast_t ast,
        aux_t aux, lexemes_t toks)
{
	LLVM_TARGET_INIT(X86);

	char *error = NULL;
	char *triple = LLVMGetDefaultTargetTriple();
	LLVMTargetRef lltarget;
	LLVMContextRef llctx = LLVMContextCreate();
	LLVMModuleRef llmod = LLVMModuleCreateWithNameInContext("oryx", llctx);
	if (LLVMGetTargetFromTriple(triple, &lltarget, &error) != 0)
		err("codegen: %s", error);
	LLVMTargetMachineRef llmach = LLVMCreateTargetMachine(
		lltarget, triple, "", "", LLVMCodeGenLevelNone, LLVMRelocDefault,
		LLVMCodeModelDefault);

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
		.mod = llmod,
		.bob = LLVMCreateBuilderInContext(llctx),
		.td  = LLVMCreateTargetDataLayout(llmach),
	};

	LLVMSetTarget(ctx.mod, triple);
	LLVMSetModuleDataLayout(ctx.mod, ctx.td);
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));
	LLVMDisposeMessage(triple);

	codegenast(ctx);

	error = NULL;
	if (LLVMVerifyModule(ctx.mod, LLVMReturnStatusAction, &error) == 1)
		err("codegen: %s", error);

	LLVMDumpModule(ctx.mod);

#if DEBUG
	tmpfree(ctx.s);
	arena_free(ctx.a);
	LLVMDisposeBuilder(ctx.bob);
	LLVMDisposeMessage(error);
	LLVMDisposeModule(ctx.mod);
	LLVMDisposeTargetData(ctx.td);
	LLVMDisposeTargetMachine(llmach);
	LLVMContextDispose(ctx.ctx);
#endif
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

	strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
	LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
	LLVMValueRef ptrval = symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v;
	*outv = LLVMBuildLoad2(ctx.bob, t, ptrval, "loadtmp");
	return fwdnode(ctx.ast, i);
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
	while (ctx.scps[ctx.scpi].i != p.lhs)
		ctx.scpi++;
	for (i = p.lhs; i <= p.rhs; i = codegenstmt(ctx, i))
		;
	return i;
}

idx_t
codegenalloca(struct cgctx ctx, idx_t i)
{
	pair_t p = ctx.ast.kids[i];
	while (ctx.scps[ctx.scpi].i != p.lhs)
		ctx.scpi++;
	for (i = p.lhs; i <= p.rhs;) {
		switch (ctx.ast.kinds[i]) {
		case ASTBLK:
			i = codegenalloca(ctx, i);
			break;
		case ASTDECL: {
			strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
			uchar *name = tmpalloc(ctx.s, sv.len + 1, 1);
			LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
			symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v =
				LLVMBuildAlloca(ctx.bob, t, svtocstr(name, sv));
		} /* fallthrough */
		default:
			i = fwdnode(ctx.ast, i);
		}
	}
	return i;
}

idx_t
codegenfunc(struct cgctx ctx, idx_t i, strview_t sv)
{
	size_t namesz = ctx.namespace.len + sv.len + 1;
	char *name = arena_new(ctx.a, char, namesz + 1);
	if (ctx.namespace.len == 0) {
		svtocstr(name, sv);
		namesz--;
	} else
		sprintf(name, "%.*s.%.*s", SV_PRI_ARGS(ctx.namespace), SV_PRI_ARGS(sv));

	ctx.namespace.p = name;
	ctx.namespace.len = namesz;

	idx_t proto = ctx.ast.kids[i].lhs;
	idx_t blk = ctx.ast.kids[i].rhs;

	snapshot_t snap = arena_snapshot_create(*ctx.a);
	for (idx_t i = ctx.ast.kids[blk].lhs; i <= ctx.ast.kids[blk].rhs;
	     i = fwdnode(ctx.ast, i))
	{
		if (ctx.ast.kinds[i] == ASTCDECL && ctx.ast.kids[i].rhs != AST_EMPTY
		    && ctx.ast.kinds[ctx.ast.kids[i].rhs] == ASTFN)
		{
			(void)codegendecl(ctx, i);
		}
	}
	arena_snapshot_restore(ctx.a, snap);

	LLVMTypeRef ret = ctx.types[i].ret == NULL
	                    ? LLVMVoidTypeInContext(ctx.ctx)
	                    : type2llvm(ctx, *ctx.types[i].ret);

	LLVMTypeRef ft = LLVMFunctionType(ret, NULL, 0, false);
	ctx.func = LLVMAddFunction(ctx.mod, name, ft);
	LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx.ctx, ctx.func,
	                                                        "entry");
	LLVMPositionBuilderAtEnd(ctx.bob, entry);

	snap = arena_snapshot_create(*ctx.a);
	(void)codegenalloca(ctx, blk);
	arena_snapshot_restore(ctx.a, snap);

	i = codegenblk(ctx, blk);
	if (ctx.ast.kids[proto].rhs == AST_EMPTY)
		LLVMBuildRetVoid(ctx.bob);

	return i;
}

idx_t
codegendecl(struct cgctx ctx, idx_t i)
{
	pair_t p = ctx.ast.kids[i];

	if (ctx.ast.kinds[i] == ASTCDECL) {
		/* Constants are purely a compiler concept; they aren’t generated
		   into anything unless they’re functions… but functions
		   shouldn’t be generated if we’re inside a function already,
		   because codegenfunc() will have already generated its child
		   functions before codegendecl() gets called. */
		if (ctx.ast.kinds[p.rhs] != ASTFN || ctx.func != NULL)
			return fwdnode(ctx.ast, i);

		return codegenfunc(ctx, p.rhs, ctx.toks.strs[ctx.ast.lexemes[i]]);
	}

	assert(ctx.ast.kinds[i] == ASTDECL);

	/* Don’t assign a default value to ‘x: int = …’ */
	if (ctx.aux.buf[p.lhs].decl.isundef)
		return fwdnode(ctx.ast, i);

	if (!ctx.types[i].isfloat && ctx.aux.buf[p.lhs].decl.isstatic) {
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
		/* TODO: Namespace the name */
		char *name = tmpalloc(ctx.s, sv.len + 1, 1);
		LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
		LLVMValueRef globl = LLVMAddGlobal(ctx.mod, t, svtocstr(name, sv));

		LLVMValueRef v;
		if (p.rhs == AST_EMPTY) {
			v = LLVMConstNull(t);
			i = fwdnode(ctx.ast, i);
		} else
			i = codegentypedexpr(ctx, p.rhs, ctx.types[i], &v);
		LLVMSetInitializer(globl, v);
		LLVMSetLinkage(globl, LLVMInternalLinkage);
		return i;
	}
	if (!ctx.types[i].isfloat /* && !aux.buf[p.lhs].decl.isstatic */) {
		LLVMValueRef var, val;
		/* TODO: Namespace the name */
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
		var = symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v;
		if (p.rhs == AST_EMPTY) {
			val = LLVMConstNull(type2llvm(ctx, ctx.types[i]));
			i = fwdnode(ctx.ast, i);
		} else
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
		if (t.size == 0)
			return LLVMIntPtrTypeInContext(ctx.ctx, ctx.td);
		assert((unsigned)t.size * 8 <= 128);
		return LLVMIntTypeInContext(ctx.ctx, t.size * 8);
	default:
		__builtin_unreachable();
	}
}
