#ifndef ORYX_CODEGEN_H
#define ORYX_CODEGEN_H

#include <gmp.h>

#include "analyzer.h"
#include "lexer.h"
#include "parser.h"

void codegen(const char *, mpq_t *, scope_t *, type_t *, ast_t, aux_t,
             lexemes_t)
	__attribute__((nonnull));

#endif /* !ORYX_CODEGEN_H */
