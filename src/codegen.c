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

static size_t codegenstmt(LLVMBuilderRef, struct ast, struct lexemes,
                          size_t);
static size_t codegenexpr(LLVMBuilderRef, struct ast, struct lexemes,
                          size_t, LLVMValueRef *)
	__attribute__((nonnull));

void
codegen(struct ast ast, struct lexemes toks)
{
	LLVMModuleRef mod = LLVMModuleCreateWithName("oryx");

	for (size_t i = 0; i < ast.len;) {
		if (ast.kinds[i] != ASTCDECL)
			err("codegen: Expected constant declaration");

		size_t expr = ast.kids[i].rhs;
		if (ast.kinds[expr] != ASTFN) {
			assert(!"not implemented");
			__builtin_unreachable();
		}

		size_t proto = ast.kids[expr].lhs, body = ast.kids[expr].rhs;

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
		LLVMBuilderRef builder = LLVMCreateBuilder();
		LLVMPositionBuilderAtEnd(builder, entry);

		free(fnname);

		for (i = ast.kids[body].lhs; i <= ast.kids[body].rhs;)
			i = codegenstmt(builder, ast, toks, i);

		LLVMDisposeBuilder(builder);
	}

	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMDumpModule(mod);
}

size_t
codegenstmt(LLVMBuilderRef builder, struct ast ast, struct lexemes toks,
            size_t i)
{
	switch (ast.kinds[i]) {
	case ASTRET:
		if (ast.kids[i].rhs == AST_EMPTY) {
			LLVMBuildRetVoid(builder);
			return i + 1;
		}
		LLVMValueRef v;
		i = codegenexpr(builder, ast, toks, ast.kids[i].rhs, &v);
		LLVMBuildRet(builder, v);
		return i;
	}

	assert(!"unreachable");
	__builtin_unreachable();
}

size_t
codegenexpr(LLVMBuilderRef builder, struct ast ast, struct lexemes toks,
            size_t i, LLVMValueRef *v)
{
	(void)builder;
	switch (ast.kinds[i]) {
	case ASTNUMLIT: {
		/* TODO: Arbitrary precision? */
		struct strview sv = toks.strs[ast.lexemes[i]];
		*v = LLVMConstIntOfStringAndSize(LLVMInt64Type(), sv.p, sv.len, 10);
		return i + 1;
	}
	}

	assert(!"unreachable");
	__builtin_unreachable();
}
