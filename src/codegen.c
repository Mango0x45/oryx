#include <assert.h>
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

static size_t codegenstmt(LLVMBuilderRef, LLVMValueRef *, struct ast,
                          struct lexemes, size_t)
	__attribute__((nonnull));
static size_t codegenexpr(LLVMBuilderRef, LLVMValueRef *, struct ast,
                          struct lexemes, size_t, LLVMValueRef *)
	__attribute__((nonnull));

static LLVMTypeRef type2llvm(struct type);

void
codegen(const char *file, struct type *types, struct ast ast,
        struct lexemes toks)
{
	LLVMModuleRef mod = LLVMModuleCreateWithName("oryx");
	LLVMSetSourceFileName(mod, file, strlen(file));
	LLVMBuilderRef builder = LLVMCreateBuilder();

	LLVMValueRef *declvals = bufalloc(NULL, ast.len, sizeof(*declvals));
	memset(declvals, 0, ast.len * sizeof(*declvals));

	for (size_t i = 0; i < ast.len;) {
		switch (ast.kinds[i]) {
		case ASTDECL: {
			/* TODO: Temporary allocator */
			struct strview sv = toks.strs[ast.lexemes[i]];
			char *name = bufalloc(NULL, sv.len + 1, 1);
			((uchar *)memcpy(name, sv.p, sv.len))[sv.len] = 0;

			LLVMTypeRef T = type2llvm(types[i]);
			LLVMValueRef globl, val;
			globl = LLVMAddGlobal(mod, T, name);
			i = codegenexpr(builder, declvals, ast, toks, ast.kids[i].rhs, &val);
			LLVMSetInitializer(globl, LLVMConstTrunc(val, T));
			free(name);
			break;
		}
		case ASTCDECL: {
			idx_t_ expr = ast.kids[i].rhs;
			if (ast.kinds[expr] != ASTFN) {
				assert(!"not implemented");
				__builtin_unreachable();
			}

			idx_t_ proto = ast.kids[expr].lhs, body = ast.kids[expr].rhs;

			LLVMTypeRef ret;
			LLVMTypeRef params[] = {0};

			if (ast.kids[proto].rhs == AST_EMPTY)
				ret = LLVMVoidType();
			else {
				size_t type = ast.kids[proto].rhs;
				struct strview sv = toks.strs[ast.lexemes[type]];

				/* TODO: Make int 32bit on 32bit platforms */
				if (strncmp("int", sv.p, sv.len) == 0)
					ret = LLVMInt64Type();
				else
					err("codegen: Unknown type: %.*s", (int)sv.len, sv.p);
			}

			LLVMTypeRef fnproto = LLVMFunctionType(ret, params, 0, false);

			struct strview sv = toks.strs[ast.lexemes[i]];
			char *fnname = bufalloc(NULL, sv.len + 1, 1);
			((char *)memcpy(fnname, sv.p, sv.len))[sv.len] = 0;

			LLVMValueRef fn = LLVMAddFunction(mod, fnname, fnproto);
			LLVMBasicBlockRef entry = LLVMAppendBasicBlock(fn, "entry");
			LLVMPositionBuilderAtEnd(builder, entry);

			free(fnname);

			for (i = ast.kids[body].lhs; i <= ast.kids[body].rhs;)
				i = codegenstmt(builder, declvals, ast, toks, i);
			break;
		}
		default:
			err("codegen: Expected declaration");
		}
	}

	free(declvals);
	LLVMDisposeBuilder(builder);

	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMDumpModule(mod);
	LLVMDisposeModule(mod);
}

size_t
codegenstmt(LLVMBuilderRef builder, LLVMValueRef *declvals, struct ast ast,
            struct lexemes toks, size_t i)
{
	switch (ast.kinds[i]) {
	case ASTRET:
		if (ast.kids[i].rhs == AST_EMPTY) {
			LLVMBuildRetVoid(builder);
			return i + 1;
		}
		LLVMValueRef v;
		i = codegenexpr(builder, declvals, ast, toks, ast.kids[i].rhs, &v);
		LLVMBuildRet(builder, v);
		return i;
	}

	assert(!"unreachable");
	__builtin_unreachable();
}

size_t
codegenexpr(LLVMBuilderRef builder, LLVMValueRef *declvals, struct ast ast,
            struct lexemes toks, size_t i, LLVMValueRef *v)
{
	(void)builder;
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

			*v = LLVMConstIntOfStringAndSize(LLVMInt64Type(), p, len, 10);
			free(p);
		} else
			*v = LLVMConstIntOfStringAndSize(LLVMInt64Type(), sv.p, sv.len, 10);
		return i + 1;
	}
	case ASTIDENT:
		err("codegen: %s: Not implemented", __func__);
	}

	assert(!"unreachable");
	__builtin_unreachable();
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
	case TYPE_F16:
	case TYPE_F32:
	case TYPE_F64:
		err("codegen: %s: Not implemented", __func__);
	case TYPE_NUM:
		assert(t.issigned);
		/* TODO: Arbitrary precision */
		if (t.size == 0)
			t.size = 8;
		return LLVMIntType(t.size * 8);
	default:
		__builtin_unreachable();
	}
}
