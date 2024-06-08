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

	struct lexemes_soa toks = lexstring(file.p, file.len);

	for (size_t i = 0; i < toks.len; i++) {
		switch (toks.kinds[i]) {
		case LEXIDENT: {
			struct strview sv = toks.strs[i];
			printf("Identifier: ‘%.*s’\n", (int)sv.len, sv.p);
			break;
		}
		case LEXAMP:
			puts("Ampersand");
			break;
		case LEXCOLON:
			puts("Colon");
			break;
		case LEXEQ:
			puts("Equals");
			break;
		case LEXLANGL:
			puts("Left angle bracket");
			break;
		case LEXLBRACE:
			puts("Left brace");
			break;
		case LEXLBRKT:
			puts("Right bracket");
			break;
		case LEXLPAR:
			puts("Left parenthesis");
			break;
		case LEXMINUS:
			puts("Minus");
			break;
		case LEXPIPE:
			puts("Pipe");
			break;
		case LEXPLUS:
			puts("Plus");
			break;
		case LEXRANGL:
			puts("Right angle bracket");
			break;
		case LEXRBRACE:
			puts("Right brace");
			break;
		case LEXRBRKT:
			puts("Right bracket");
			break;
		case LEXRPAR:
			puts("Right parenthesis");
			break;
		case LEXSEMI:
			puts("Semicolon");
			break;
		case LEXSLASH:
			puts("Slash");
			break;
		case LEXSTAR:
			puts("Asterisk");
			break;
		case LEXTILDE:
			puts("Tilde");
			break;
		}
	}

#if DEBUG
	free(file.p);
	lexemes_free(toks);
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
