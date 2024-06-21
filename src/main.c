#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gmp.h>

#include "alloc.h"
#include "analyzer.h"
#include "codegen.h"
#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"

static char *readfile(const char *, size_t *)
	__attribute__((returns_nonnull, nonnull));

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("Usage: oryx file\n", stderr);
		exit(EXIT_FAILURE);
	}

	size_t srclen;
	char *src = readfile(argv[1], &srclen);

	arena a = NULL;
	mpq_t *folds;
	struct type *types;
	struct scope *scps;

	struct lexemes toks = lexstring(src, srclen);
	struct ast ast = parsetoks(toks);
	analyzeprog(ast, toks, &a, &types, &scps, &folds);
	codegen(argv[1], folds, scps, types, ast, toks);

#if DEBUG
	for (size_t i = 0; i < ast.len; i++) {
		if ((*folds[i])._mp_den._mp_d != NULL)
			mpq_clear(folds[i]);
	}

	free(folds);
	free(scps);
	free(src);
	free(types);
	lexemes_free(toks);
	ast_free(ast);
	arena_free(&a);
#endif
	return EXIT_SUCCESS;
}

char *
readfile(const char *filename, size_t *n)
{
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
		err("open: %s", filename);

	struct stat sb;
	if (fstat(fd, &sb) == -1)
		err("fstat: %s", filename);

	char *p = bufalloc(NULL, sb.st_size + 4, 1);

	ssize_t nr;
	for (size_t off = 0; (nr = read(fd, p + off, sb.st_blksize)) > 0; off += nr)
		;
	if (nr == -1)
		err("read: %s", filename);
	for (int i = 0; i < 4; i++)
		p[sb.st_size + i] = 0;

	*n = sb.st_size;
	close(fd);
	return p;
}
