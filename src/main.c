#include <sys/stat.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "errors.h"
#include "lexer.h"

static char *readfile(const char *, size_t *);

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fputs("Usage: oryx file\n", stderr);
		exit(EXIT_FAILURE);
	}

	struct {
		char *p;
		size_t len;
	} file;
	file.p = readfile(argv[1], &file.len);

	struct {
		struct lexeme *p;
		size_t len;
	} toks;
	toks.p = lexstring(file.p, file.len, &toks.len);

#if DEBUG
	free(file.p);
	free(toks.p);
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

	char *p = malloc(sb.st_size + 4);
	if (p == NULL)
		err("malloc:");

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
