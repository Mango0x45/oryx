#ifndef ORYX_CODEGEN_H
#define ORYX_CODEGEN_H

#include "analyzer.h"
#include "lexer.h"
#include "parser.h"

void codegen(const char *, struct scope *, struct type *, struct ast,
             struct lexemes)
	__attribute__((nonnull));

#endif /* !ORYX_CODEGEN_H */
