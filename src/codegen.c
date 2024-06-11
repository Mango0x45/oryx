#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

#include "codegen.h"

void
codegen(struct ast_soa ast, struct lexemes_soa toks)
{
	(void)ast;
	(void)toks;

	/* Create a new module */
	LLVMModuleRef mod = LLVMModuleCreateWithName("oryx");

	/* Declare the function ‘sum :: (int, int) int’ */
	LLVMTypeRef param_types[] = {LLVMInt32Type(), LLVMInt32Type()};
	LLVMTypeRef ret_type = LLVMFunctionType(LLVMInt32Type(), param_types, 2, 0);
	LLVMValueRef sum = LLVMAddFunction(mod, "sum", ret_type);

	/* Create an entry block, and a builder */
	LLVMBasicBlockRef entry = LLVMAppendBasicBlock(sum, "entry");
	LLVMBuilderRef builder = LLVMCreateBuilder();
	LLVMPositionBuilderAtEnd(builder, entry);

	/* Generate an ADD instruction */
	LLVMValueRef tmp = LLVMBuildAdd(builder, LLVMGetParam(sum, 0),
	                                LLVMGetParam(sum, 1), "tmpadd");
	LLVMBuildRet(builder, tmp);

	/* Verify that all went well and if not we abort */
	char *error = NULL;
	LLVMVerifyModule(mod, LLVMAbortProcessAction, &error);
	LLVMDisposeMessage(error);

	LLVMDumpModule(mod);

	LLVMDisposeBuilder(builder);
}
