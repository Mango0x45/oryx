#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "alloc.h"
#include "codegen.h"
#include "common.h"
#include "errors.h"
#include "parser.h"
#include "types.h"

struct cgctx {
	LLVMModuleRef mod;
	LLVMBuilderRef bob;
	struct strview namespace;
};

static size_t codegendecl(struct cgctx, struct type *, struct ast,
                          struct lexemes, size_t)
	__attribute__((nonnull));
static size_t codegenexpr(struct cgctx, struct type *, struct ast,
                          struct lexemes, size_t, LLVMValueRef *)
	__attribute__((nonnull));

static LLVMTypeRef type2llvm(struct type);

/* TODO: Don’t do this? */
#define lengthof(xs) (sizeof(xs) / sizeof(*(xs)))
static struct {
	struct strview key;
	LLVMValueRef val;
} constants[1024];
static size_t constcnt;

void
codegen(const char *file, struct type *types, struct ast ast,
        struct lexemes toks)
{
	struct cgctx ctx = {0};
	ctx.mod = LLVMModuleCreateWithName("oryx");
	ctx.bob = LLVMCreateBuilder();
	LLVMSetSourceFileName(ctx.mod, file, strlen(file));

	for (size_t i = 0; i < ast.len;) {
		// LLVMValueRef val;
		assert(ast.kinds[i] == ASTDECL || ast.kinds[i] == ASTCDECL);
		i = codegendecl(ctx, types, ast, toks, i);

		/* TODO: Temporary allocator */
		// struct strview sv = toks.strs[ast.lexemes[i]];
		// char *name = bufalloc(NULL, sv.len + 1, 1);
		// ((uchar *)memcpy(name, sv.p, sv.len))[sv.len] = 0;
		//
		// LLVMValueRef globl, init;
		// LLVMTypeRef vartype = type2llvm(types[i]);
		//
		// globl = LLVMAddGlobal(mod, vartype, name);
		// LLVMSetGlobalConstant(globl, ast.kinds[i] == ASTCDECL);
		//
		// if (ast.kids[i].rhs != AST_EMPTY) {
		// 	i = codegenexpr(bob, types, ast, toks, ast.kids[i].rhs, &init);
		// 	init = LLVMConstTrunc(init, vartype);
		// } else {
		// 	init = LLVMConstNull(vartype);
		// 	i = fwdnode(ast, i);
		// }
		//
		// LLVMSetInitializer(globl, init);
		// LLVMSetLinkage(globl, LLVMPrivateLinkage);
		//
		// free(name);
	}

	LLVMDisposeBuilder(ctx.bob);

	char *error = NULL;
	LLVMVerifyModule(ctx.mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMDumpModule(ctx.mod);
	LLVMDisposeModule(ctx.mod);
}

size_t
codegendecl(struct cgctx ctx, struct type *types, struct ast ast,
            struct lexemes toks, size_t i)
{
	struct strview ident = toks.strs[ast.lexemes[i]];

	char *name;
	if (ctx.namespace.len != 0) {
		size_t namelen = ident.len + ctx.namespace.len + 1;
		name = bufalloc(NULL, namelen + 1, 1);
		sprintf(name, "%.*s.%.*s", (int)ctx.namespace.len, ctx.namespace.p,
				(int)ident.len, ident.p);
	} else {
		name = bufalloc(NULL, ident.len + 1, 1);
		memcpy(name, ident.p, ident.len);
		name[ident.len] = 0;
	}

	LLVMValueRef val;
	LLVMTypeRef vartype = type2llvm(types[i]);

	if (ast.kids[i].rhs != AST_EMPTY) {
		i = codegenexpr(ctx, types, ast, toks, ast.kids[i].rhs, &val);
		val = LLVMConstTrunc(val, vartype);
	} else {
		i = fwdnode(ast, i);
		val = LLVMConstNull(vartype);
	}

	LLVMValueRef globl = LLVMAddGlobal(ctx.mod, vartype, name);
	LLVMSetInitializer(globl, val);
	LLVMSetLinkage(globl, LLVMLinkerPrivateLinkage);

	free(name);
	return i;
}

size_t
codegenexpr(struct cgctx ctx, struct type *types, struct ast ast,
            struct lexemes toks, size_t i, LLVMValueRef *v)
{
	(void)ctx;
	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		/* TODO: Arbitrary precision? */
		struct strview sv = toks.strs[ast.lexemes[i]];

		bool has_sep = memchr(sv.p, '\'', sv.len) != NULL;

		/* TODO: Temporary one-time-use allocator? */
		if (has_sep) {
			size_t len = 0;
			char *p = bufalloc(NULL, sv.len, 1);
			for (size_t i = 0; i < sv.len; i++) {
				if (sv.p[i] != '\'')
					p[len++] = sv.p[i];
			}

			*v = LLVMConstIntOfStringAndSize(type2llvm(types[i]), p, len, 10);
			free(p);
		} else {
			*v = LLVMConstIntOfStringAndSize(type2llvm(types[i]), sv.p, sv.len,
			                                 10);
		}
		return i + 1;
	}
	case ASTIDENT:
		err("codegen: %s: Not implemented", __func__);
	default:
		assert(!"unreachable");
		__builtin_unreachable();
	}
}

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
	case TYPE_F16:
		return LLVMHalfType();
	case TYPE_F32:
		return LLVMFloatType();
	case TYPE_F64:
		return LLVMDoubleType();
	case TYPE_NUM:
		assert(t.issigned);
		/* TODO: Arbitrary precision */
		if (t.size == 0)
			return LLVMInt64Type();
		return LLVMIntType((unsigned)t.size * 8);
	default:
		__builtin_unreachable();
	}
}
