#ifndef ORYX_CODEGEN_H
#define ORYX_CODEGEN_H

#include "lexer.h"
#include "parser.h"

void codegen(struct ast ast, struct lexemes toks);

#endif /* !ORYX_CODEGEN_H */
