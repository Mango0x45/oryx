#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "parser.h"

/* #define AST_DFLT_CAP (2048) */
#define AST_DFLT_CAP (8)
#define SIZE_WDTH    (sizeof(size_t) * CHAR_BIT)

static struct ast_soa mk_ast_soa(void);
static void ast_soa_resz(struct ast_soa *);

struct ast_soa
parsetoks(struct lexemes_soa toks)
{
	struct ast_soa ast = mk_ast_soa();

	return ast;
}

struct ast_soa
mk_ast_soa(void)
{
	struct ast_soa soa;

	static_assert(offsetof(struct ast_soa, kinds)
	                  < offsetof(struct ast_soa, lexemes),
	              "KINDS is not the first field before LEXEMES");
	static_assert(offsetof(struct ast_soa, kinds)
	                  < offsetof(struct ast_soa, kids),
	              "KINDS is not the first field before KIDS");
	static_assert(AST_DFLT_CAP * sizeof(*soa.kinds) % alignof(*soa.lexemes)
	                  == 0,
	              "Additional padding is required to properly align LEXEMES");
	static_assert(AST_DFLT_CAP * (sizeof(*soa.kinds) + sizeof(*soa.lexemes))
	                      % alignof(*soa.kids)
	                  == 0,
	              "Additional padding is required to properly align KIDS");

	soa.len = 0;
	soa.cap = AST_DFLT_CAP;

	if ((soa.kinds = malloc(soa.cap * AST_SOA_BLKSZ)) == NULL)
		err("malloc:");
	soa.lexemes = (void *)((char *)soa.kinds + soa.cap * sizeof(*soa.kinds));
	soa.kids = (void *)((char *)soa.lexemes + soa.cap * sizeof(*soa.lexemes));

	return soa;
}

void
ast_soa_resz(struct ast_soa *soa)
{
	static_assert(offsetof(struct ast_soa, kinds)
	                  < offsetof(struct ast_soa, lexemes),
	              "KINDS is not the first field before LEXEMES");
	static_assert(offsetof(struct ast_soa, kinds)
	                  < offsetof(struct ast_soa, kids),
	              "KINDS is not the first field before KIDS");
	static_assert(offsetof(struct ast_soa, lexemes)
	                  < offsetof(struct ast_soa, kids),
	              "LEXEMES is not the second field before KIDS");

	size_t ncap, pad1, pad2, newsz;
	ptrdiff_t lexemes_off, kids_off;

	lexemes_off = (char *)soa->lexemes - (char *)soa->kinds;
	kids_off = (char *)soa->kids - (char *)soa->kinds;

	/* The capacity is always going to be a power of 2, so checking for overflow
	   becomes pretty trivial */
	if ((soa->cap >> (SIZE_WDTH - 1)) != 0) {
		errno = EOVERFLOW;
		err("ast_soa_resz:");
	}
	ncap = soa->cap << 1;

	/* Ensure that soa->lexemes is properly aligned */
	pad1 = alignof(*soa->lexemes)
	     - ncap * sizeof(*soa->kinds) % alignof(*soa->lexemes);
	if (pad1 == alignof(*soa->lexemes))
		pad1 = 0;

	pad2 = alignof(*soa->kids)
	     - (ncap * (sizeof(*soa->kinds) + sizeof(*soa->lexemes)) + pad1)
	           % alignof(*soa->kids);
	if (pad2 != alignof(*soa->kids))
		pad2 = 0;

	newsz = ncap * AST_SOA_BLKSZ + pad1 + pad2;

	if ((soa->kinds = realloc(soa->kinds, newsz)) == NULL)
		err("realloc:");

	soa->lexemes = (void *)((char *)soa->kinds + ncap * sizeof(*soa->kinds)
	                        + pad1);
	soa->kids = (void *)((char *)soa->lexemes + ncap * sizeof(*soa->lexemes)
	                     + pad2);

	memmove(soa->kids, (char *)soa->kinds + kids_off,
	        soa->len * sizeof(*soa->kids));
	memmove(soa->lexemes, (char *)soa->kinds + lexemes_off,
	        soa->len * sizeof(*soa->lexemes));

	soa->cap = ncap;
}
