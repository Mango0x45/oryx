#ifndef ORYX_CODEGEN_H
#define ORYX_CODEGEN_H

#include "lexer.h"
#include "parser.h"

void codegen(struct ast_soa ast, struct lexemes_soa toks);

#endif /* !ORYX_CODEGEN_H */
