#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gmp.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/ExecutionEngine.h>

#include "alloc.h"
#include "analyzer.h"
#include "bitset.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "strview.h"

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

	ast_t     ast;
	aux_t     aux;
	bitset_t *cnst;
	fold_t   *folds;
	lexemes_t toks;
	scope_t  *scps;
	type_t  **types;

	LLVMBuilderRef    bob;
	LLVMContextRef    ctx;
	LLVMModuleRef     mod;
	LLVMTargetDataRef td;
	LLVMValueRef      func;

	idx_t scpi;
	strview_t decl, namespace;
};

static void codegenast(struct cgctx);
static LLVMTypeRef type2llvm(struct cgctx, type_t *)
	__attribute__((nonnull));
static symval_t *symtab_get_from_scopes(struct cgctx ctx, strview_t sv);

extern bool lflag, sflag;
extern const char *oflag;

void
codegen(const char *file, bitset_t *cnst, fold_t *folds, scope_t *scps,
        type_t **types, ast_t ast, aux_t aux, lexemes_t toks)
{
	LLVM_TARGET_INIT(AArch64);
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

		.ast   = ast,
		.aux   = aux,
		.cnst  = cnst,
		.folds = folds,
		.scps  = scps,
		.toks  = toks,
		.types = types,

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
	LLVMDisposeMessage(error);

	if (lflag) {
		/* Needlessly inefficient… but that’s LLVM for you */
		char *s = LLVMPrintModuleToString(llmod);
		fputs(s, stdout);
		LLVMDisposeMessage(s);
	} else {
		LLVMCodeGenFileType ft;
		const char *dst = oflag == NULL ? "out.o" : oflag;

		if (sflag) {
			size_t n = strlen(dst);
			char *buf = memcpy(tmpalloc(ctx.s, n + 1, 1), dst, n);
			buf[n - 1] = 's';
			buf[n - 0] = 0;
			dst = buf;
			ft = LLVMAssemblyFile;
		} else
			ft = LLVMObjectFile;
		LLVMTargetMachineEmitToFile(llmach, llmod, dst, ft, &error);
	}

#if DEBUG
	tmpfree(ctx.s);
	arena_free(ctx.a);
	LLVMDisposeBuilder(ctx.bob);
	LLVMDisposeModule(ctx.mod);
	LLVMDisposeTargetData(ctx.td);
	LLVMDisposeTargetMachine(llmach);
	LLVMContextDispose(ctx.ctx);
#endif
}

static idx_t codegendecl(struct cgctx ctx, idx_t);

idx_t
codegentypedexpr(struct cgctx ctx, idx_t i, type_t *T, LLVMValueRef *outv)
{
	/* To avoid spamming NULL checks everywhere */
	if (T == NULL || outv == NULL)
		goto callstmt;

	if (T->kind == TYPE_NUM && TESTBIT(ctx.cnst, i) && !T->isfloat) {
		char buf[128/4 + 1];
		mpz_get_str(buf, 16, mpq_numref(ctx.folds[i].q));
		*outv = LLVMConstIntOfString(type2llvm(ctx, T), buf, 16);
		return fwdnode(ctx.ast, i);
	} else if (T->kind == TYPE_NUM && TESTBIT(ctx.cnst, i)) {
		char *s, *buf;
		size_t len;
		mpf_t x;
		mp_exp_t e;
		mp_bitcnt_t prec;

		/* TODO: Is this even correct? */
		switch (T->size) {
		case 2:
			prec = 5;
			break;
		case 4:
			prec = 8;
			break;
		case 8:
			prec = 11;
			break;
		case 16:
			prec = 16;
			break;
		default:
			__builtin_unreachable();
		}

		mpf_init2(x, prec);
		mpf_set_q(x, ctx.folds[i].q);

		s = mpf_get_str(NULL, &e, 10, 0, x);
		len = strlen(s);
		buf = tmpalloc(ctx.s, len + 2, 1);
		for (size_t i = 0; i < (size_t)e; i++)
			buf[i] = s[i];
		buf[e] = '.';
		for (size_t i = e; i < len; i++)
			buf[i + 1] = s[i];
		buf[len + 1] = 0;
		*outv = LLVMConstRealOfString(type2llvm(ctx, T), buf);

		free(s);
		mpf_clear(x);
		return fwdnode(ctx.ast, i);
	} else if (T->kind == TYPE_BOOL && TESTBIT(ctx.cnst, i)) {
		LLVMTypeRef T = LLVMInt1TypeInContext(ctx.ctx);
		*outv = LLVMConstInt(T, ctx.folds[i].b, false);
		return fwdnode(ctx.ast, i);
	}

	switch (ctx.ast.kinds[i]) {
	case ASTIDENT: {
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];

		/* Allow shadowing in decls like ‘x := x + 1’ */
		if (strview_eq(sv, ctx.decl))
			ctx.scpi--;

		LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
		LLVMValueRef ptrval = symtab_get_from_scopes(ctx, sv)->v;
		*outv = LLVMBuildLoad2(ctx.bob, t, ptrval, "load");
		return fwdnode(ctx.ast, i);
	}
	case ASTUNPLUS:
		return codegentypedexpr(ctx, ctx.ast.kids[i].rhs, T, outv);
	case ASTUNCMPL: {
		LLVMValueRef v, minus_one;
		minus_one = LLVMConstInt(type2llvm(ctx, ctx.types[i]), -1, false);
		idx_t ni = codegentypedexpr(ctx, ctx.ast.kids[i].rhs, ctx.types[i], &v);
		*outv = LLVMBuildXor(ctx.bob, v, minus_one, "cmpl");
		return ni;
	}
	case ASTUNNEG: {
		LLVMValueRef v;
		idx_t ni = codegentypedexpr(ctx, ctx.ast.kids[i].rhs, ctx.types[i], &v);
		*outv = LLVMBuildNeg(ctx.bob, v, "neg");
		return ni;
	}
	case ASTBINADD:
	case ASTBINAND:
	case ASTBINDIV:
	case ASTBINIOR:
	case ASTBINMOD:
	case ASTBINMUL:
	case ASTBINSHL:
	case ASTBINSHR:
	case ASTBINSUB:
	case ASTBINXOR: {
		static const struct binop {
			LLVMOpcode op[3];
			const char *name;
		} binoptbl[UINT8_MAX + 1] = {
			['+']       = {{LLVMAdd,  LLVMAdd,  LLVMAdd},  "add"},
			['&']       = {{LLVMAnd,  LLVMAnd,  -1},       "and"},
			['*']       = {{LLVMMul,  LLVMMul,  LLVMFMul}, "mul"},
			['|']       = {{LLVMOr,   LLVMOr,   -1},       "ior"},
			['-']       = {{LLVMSub,  LLVMSub,  LLVMFSub}, "sub"},
			['/']       = {{LLVMUDiv, LLVMSDiv, LLVMFDiv}, "div"},
			['%']       = {{LLVMURem, LLVMSRem, -1},       "rem"},
			['~']       = {{LLVMXor,  LLVMXor,  -1},       "xor"},
			[ASTBINSHL] = {{LLVMShl,  LLVMShl,  -1},       "shl"},
			[ASTBINSHR] = {{LLVMLShr, LLVMLShr, -1},       "shr"},
		};

		idx_t lhs = ctx.ast.kids[i].lhs, rhs = ctx.ast.kids[i].rhs;
		LLVMValueRef vl, vr;
		(void)codegentypedexpr(ctx, lhs, ctx.types[i], &vl);
		idx_t ni = codegentypedexpr(ctx, rhs, ctx.types[i], &vr);

		if (ctx.ast.kinds[i] >= ASTBINSHL && ctx.types[rhs]->size != 0) {
			vr = LLVMBuildIntCast2(ctx.bob, vr, type2llvm(ctx, ctx.types[lhs]),
			                       false, "cast");
		}

		struct binop bo = binoptbl[ctx.ast.kinds[i]];
		*outv = LLVMBuildBinOp(
			ctx.bob, bo.op[ctx.types[i]->isfloat ? 2 : ctx.types[i]->issigned],
			vl, vr, bo.name);
		return ni;
	}
	case ASTBINEQ:
	case ASTBINNEQ: {
		static const struct binop {
			LLVMIntPredicate pred;
			const char *name;
		} binoptbl[UINT8_MAX + 1] = {
			[ASTBINEQ]  = {LLVMIntEQ, "ieq"},
			[ASTBINNEQ] = {LLVMIntNE, "ine"},
		};

		idx_t lhs = ctx.ast.kids[i].lhs, rhs = ctx.ast.kids[i].rhs;
		LLVMValueRef vl, vr;
		(void)codegentypedexpr(ctx, lhs, ctx.types[i], &vl);
		idx_t ni = codegentypedexpr(ctx, rhs, ctx.types[i], &vr);
		struct binop bo = binoptbl[ctx.ast.kinds[i]];
		*outv = LLVMBuildICmp(ctx.bob, bo.pred, vl, vr, bo.name);
		return ni;
	}
callstmt:
	case ASTFUNCALL: {
		idx_t lhs = ctx.ast.kids[i].lhs;
		assert(ctx.ast.kinds[lhs] == ASTIDENT);
		strview_t sv = ctx.toks.strs[ctx.ast.lexemes[lhs]];
		symval_t *sym = symtab_get_from_scopes(ctx, sv);
		LLVMTypeRef ft = LLVMGlobalGetValueType(sym->v);
		LLVMValueRef call = LLVMBuildCall2(ctx.bob, ft, sym->v, NULL, 0,
		                                   outv == NULL ? "" : "call");
		if (outv != NULL)
			*outv = call;
		return fwdnode(ctx.ast, i);
	}
	default:
		__builtin_unreachable();
	}
}

idx_t
codegenstmt(struct cgctx ctx, idx_t i)
{
	switch (ctx.ast.kinds[i]) {
	case ASTDECL:
	case ASTCDECL:
		return codegendecl(ctx, i);
	case ASTASIGN: {
		pair_t p = ctx.ast.kids[i];
		assert(ctx.ast.kinds[p.lhs] == ASTIDENT);
		LLVMValueRef var, val;
		var = symtab_get_from_scopes(ctx, ctx.toks.strs[ctx.ast.lexemes[p.lhs]])
		          ->v;
		i = codegentypedexpr(ctx, p.rhs, ctx.types[i], &val);
		LLVMBuildStore(ctx.bob, val, var);
		return i;
	}
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
	case ASTCALLSTMT:
		return codegentypedexpr(ctx, i + 1, NULL, NULL);
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
			if (!ctx.aux.buf[ctx.ast.kids[i].lhs].decl.isstatic) {
				strview_t sv = ctx.toks.strs[ctx.ast.lexemes[i]];
				uchar *name = tmpalloc(ctx.s, sv.len + 1, 1);
				LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
				symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v =
					LLVMBuildAlloca(ctx.bob, t, svtocstr(name, sv));
			}
		} /* fallthrough */
		default:
			i = fwdnode(ctx.ast, i);
		}
	}
	return i;
}

idx_t
codegenfunc(struct cgctx ctx, idx_t i, strview_t sv, LLVMValueRef *outv)
{
	size_t namesz = ctx.namespace.len + sv.len + 1;
	char *name = arena_new(ctx.a, char, namesz + 1);
	if (ctx.namespace.len == 0) {
		svtocstr(name, sv);
		namesz--;
	} else {
		snprintf(name, namesz + 1, "%.*s.%.*s", SV_PRI_ARGS(ctx.namespace),
		         SV_PRI_ARGS(sv));
	}

	ctx.namespace.p = name;
	ctx.namespace.len = namesz;

	idx_t proto = ctx.ast.kids[i].lhs;
	idx_t blk = ctx.ast.kids[i].rhs;

	while (ctx.scps[ctx.scpi].i != ctx.ast.kids[blk].lhs)
		ctx.scpi++;

	snapshot_t snap = arena_snapshot_create(*ctx.a);
	for (idx_t i = ctx.ast.kids[blk].lhs; i <= ctx.ast.kids[blk].rhs;
	     i = fwdnode(ctx.ast, i))
	{
		if (ctx.ast.kinds[i] == ASTCDECL
		    && ctx.ast.kinds[ctx.ast.kids[i].rhs] == ASTFN)
		{
			(void)codegendecl(ctx, i);
		}
	}
	arena_snapshot_restore(ctx.a, snap);

	LLVMTypeRef ret = ctx.types[i]->ret == NULL
	                    ? LLVMVoidTypeInContext(ctx.ctx)
	                    : type2llvm(ctx, ctx.types[i]->ret);

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

	*outv = ctx.func;
	return i;
}

idx_t
codegendecl(struct cgctx ctx, idx_t i)
{
	pair_t p = ctx.ast.kids[i];
	strview_t sv = ctx.decl = ctx.toks.strs[ctx.ast.lexemes[i]];

	if (ctx.ast.kinds[i] == ASTCDECL) {
		/* Constants are purely a compiler concept; they aren’t generated
		   into anything unless they’re functions… but functions
		   shouldn’t be generated if we’re inside a function already,
		   because codegenfunc() will have already generated its child
		   functions before codegendecl() gets called. */
		if (ctx.ast.kinds[p.rhs] != ASTFN || ctx.func != NULL)
			return fwdnode(ctx.ast, i);

		symval_t *sym = symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL);
		return codegenfunc(ctx, p.rhs, ctx.toks.strs[ctx.ast.lexemes[i]], &sym->v);
	}

	assert(ctx.ast.kinds[i] == ASTDECL);

	/* Don’t assign a default value to ‘x: int = …’ */
	if (ctx.aux.buf[p.lhs].decl.isundef)
		return fwdnode(ctx.ast, i);

	if (ctx.aux.buf[p.lhs].decl.isstatic) {
		/* TODO: Namespace the name */
		char *name;
		if (ctx.namespace.len != 0) {
			size_t bufsz = ctx.namespace.len + sv.len + 2;
			name = tmpalloc(ctx.s, bufsz, 1);
			snprintf(name, bufsz, "%.*s.%.*s", SV_PRI_ARGS(ctx.namespace),
					 SV_PRI_ARGS(sv));
		} else {
			name = tmpalloc(ctx.s, sv.len + 1, 1);
			svtocstr(name, sv);
		}

		LLVMTypeRef t = type2llvm(ctx, ctx.types[i]);
		LLVMValueRef globl = LLVMAddGlobal(ctx.mod, t, name);
		symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v = globl;

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

	/* Non-static, non-undef, mutable */

	LLVMValueRef var, val;
	var = symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL)->v;
	if (p.rhs == AST_EMPTY) {
		val = LLVMConstNull(type2llvm(ctx, ctx.types[i]));
		i = fwdnode(ctx.ast, i);
	} else
		i = codegentypedexpr(ctx, p.rhs, ctx.types[i], &val);
	LLVMBuildStore(ctx.bob, val, var);
	return i;
}

void
codegenast(struct cgctx ctx)
{
	for (idx_t i = 0; i < ctx.ast.len; i = codegendecl(ctx, i))
		;
}

LLVMTypeRef
type2llvm(struct cgctx ctx, type_t *T)
{
	switch (T->kind) {
	case TYPE_BOOL:
		return LLVMInt1TypeInContext(ctx.ctx);
	case TYPE_NUM:
		assert(T->size != 0);
		assert((unsigned)T->size * 8 <= 128);
		if (!T->isfloat)
			return LLVMIntTypeInContext(ctx.ctx, T->size * 8);
		switch (T->size) {
		case 2:
			return LLVMHalfTypeInContext(ctx.ctx);
		case 4:
			return LLVMFloatTypeInContext(ctx.ctx);
		case 8:
			return LLVMDoubleTypeInContext(ctx.ctx);
		case 16:
			return LLVMFP128TypeInContext(ctx.ctx);
		default:
			__builtin_unreachable();
		}
	default:
		__builtin_unreachable();
	}
}

symval_t *
symtab_get_from_scopes(struct cgctx ctx, strview_t sv)
{
	for (;;) {
		symval_t *p = symtab_insert(&ctx.scps[ctx.scpi].map, sv, NULL);
		if (p != NULL || ctx.scpi == 0)
			return p;
		ctx.scpi = ctx.scps[ctx.scpi].up;
	}
}
