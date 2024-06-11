#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "alloc.h"
#include "errors.h"

void *
bufalloc(void *ptr, size_t nmemb, size_t size)
{
	if (size > SIZE_MAX / nmemb) {
		errno = EOVERFLOW;
		err("%s:", __func__);
	}
	if ((ptr = realloc(ptr, nmemb * size)) == NULL)
		err("%s:", __func__);
	return ptr;
}
