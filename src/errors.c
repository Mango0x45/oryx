#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"

void
err(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);

	int save = errno;
	flockfile(stderr);

	fputs("oryx: ", stderr);
	vfprintf(stderr, fmt, ap);
	if (fmt[strlen(fmt) - 1] == ':')
		fprintf(stderr, " %s", strerror(save));
	fputc('\n', stderr);
	fflush(stderr);
	funlockfile(stderr);
	va_end(ap);
	exit(EXIT_FAILURE);
}
