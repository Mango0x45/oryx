#include <stddef.h>
#include <stdint.h>

#include "alloc.h"
#include "symtab.h"
#include "strview.h"

struct symtab {
	symtab_t *child[4];
	strview_t key;
	symval_t val;
};

struct typetab {
	typetab_t *child[4];
	strview_t key;
	type_t *val;
};

symval_t *
symtab_insert(symtab_t **m, strview_t sv, arena_t *a)
{
	for (uint64_t h = strview_hash(sv); *m; h <<= 2) {
		if (strview_eq(sv, (*m)->key))
			return &(*m)->val;
		m = &(*m)->child[h >> 62];
	}
	if (a == NULL)
		return NULL;
	*m = arena_new(a, symtab_t, 1);
	(*m)->key = sv;
	return &(*m)->val;
}

type_t **
typetab_insert(typetab_t **m, strview_t sv, arena_t *a)
{
	for (uint64_t h = strview_hash(sv); *m; h <<= 2) {
		if (strview_eq(sv, (*m)->key))
			return &(*m)->val;
		m = &(*m)->child[h >> 62];
	}
	if (a == NULL)
		return NULL;
	*m = arena_new(a, typetab_t, 1);
	(*m)->key = sv;
	return &(*m)->val;
}
