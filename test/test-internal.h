#ifndef ORYX_TEST_INTERNAL_H
#define ORYX_TEST_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>

static int rv;

#define oryx_assert(expr)                                                      \
	do {                                                                       \
		if (!(expr)) {                                                         \
			fprintf(stderr, "%s:%d: Test ‘%s’ failed\n", __FILE__, __LINE__,   \
			        __func__);                                                 \
			fprintf(stderr, "\tFailing expression: ‘%s’\n", #expr);            \
			rv = EXIT_FAILURE;                                                 \
			goto cleanup;                                                      \
		}                                                                      \
	} while (0)

#endif /* !ORYX_TEST_INTERNAL_H */
