#ifndef ORYX_SYMTAB_H
#define ORYX_SYMTAB_H

#include <stdbool.h>

#include <llvm-c/Types.h>

#include "alloc.h"
#include "strview.h"
#include "types.h"

typedef struct symtab symtab_t;

typedef struct {
	bool exists;
	idx_t i;
	LLVMValueRef v;
} symval_t;

/* Index the symbol table M with the key SV, returning a pointer to the
   value.  If no entry exists and A is non-null, a pointer to a newly
   allocated (and zeroed) value is returned, NULL otherwise. */
symval_t *symtab_insert(symtab_t **m, strview_t sv, arena_t *a)
	__attribute__((nonnull(1)));

#endif /* !ORYX_SYMTAB_H */
