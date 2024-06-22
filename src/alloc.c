#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "alloc.h"
#include "common.h"
#include "errors.h"

void *
bufalloc(void *ptr, size_t nmemb, size_t size)
{
	assert(nmemb * size != 0);
	if (unlikely(size > SIZE_MAX / nmemb)) {
		errno = ENOMEM;
		err("%s:", __func__);
	}
	if (unlikely((ptr = realloc(ptr, nmemb * size)) == NULL))
		err("%s:", __func__);
	return ptr;
}
