#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gmp.h>
#define OPTPARSE_API static
#define OPTPARSE_IMPLEMENTATION
#include <optparse.h>

#include "alloc.h"
#include "analyzer.h"
#include "bitset.h"
#include "codegen.h"
#include "common.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"

/* Read the contents of FILE into a dynamically allocated buffer and
   return it, storing the buffer size in BUFSZ. */
static char *readfile(const char *file, size_t *bufsz)
	__attribute__((returns_nonnull, nonnull));

bool lflag, sflag;
const char *oflag;

int
main(int argc, char **argv)
{
	struct optparse_long longopts[] = {
		{"assembly",  's', OPTPARSE_NONE},
		{"emit-llvm", 'l', OPTPARSE_NONE},
		{"output",    'o', OPTPARSE_REQUIRED},
		{0},
	};

	int opt;
	struct optparse opts;
	optparse_init(&opts, argv);

	while ((opt = optparse_long(&opts, longopts, NULL)) != -1) {
		switch (opt) {
		case 'o':
			oflag = opts.optarg;
			break;
		case 'l':
			lflag = true;
			break;
		case 's':
			sflag = true;
			break;
		default:
			/* TODO: Add warn() to errors.h */
			fprintf(stderr, "oryx: %s\n", opts.errmsg);
usage:
			fprintf(stderr, "Usage: oryx [-l | -s] [-o file] source\n");
			exit(EXIT_FAILURE);
		}
	}

	if (lflag && sflag)
		goto usage;

	argc -= opts.optind;
	argv += opts.optind;

	if (argc != 1)
		goto usage;

	size_t srclen;
	char *src = readfile(argv[0], &srclen);

	aux_t aux;
	fold_t *folds;
	scope_t *scps;
	bitset_t *cnst;
	arena_t a = NULL;

	lexemes_t toks = lexstring(src, srclen);
	ast_t ast = parsetoks(toks, &aux);
	type_t **types = analyzeprog(ast, aux, toks, &a, &scps, &folds, &cnst);
	codegen(argv[0], cnst, folds, scps, types, ast, aux, toks);

#if DEBUG
	for (size_t i = 0; i < ast.len; i++) {
		if ((*folds[i].q)._mp_den._mp_d != NULL)
			mpq_clear(folds[i].q);
	}

	free(folds);
	free(scps);
	free(types);
	free(src);
	lexemes_free(toks);
	ast_free(ast);
	aux_free(aux);
	arena_free(&a);
#endif
	return EXIT_SUCCESS;
}

char *
readfile(const char *filename, size_t *n)
{
	int fd = open(filename, O_RDONLY);
	if (fd == -1)
		err("open: %s:", filename);

	struct stat sb;
	if (fstat(fd, &sb) == -1)
		err("fstat: %s:", filename);

	char *p = bufalloc(NULL, sb.st_size + 4, 1);

	ssize_t nr;
	for (size_t off = 0; (nr = read(fd, p + off, sb.st_blksize)) > 0; off += nr)
		;
	if (nr == -1)
		err("read: %s:", filename);

	p[sb.st_size + 0] =
	p[sb.st_size + 1] =
	p[sb.st_size + 2] =
	p[sb.st_size + 3] = 0;

	*n = sb.st_size;
	close(fd);
	return p;
}
