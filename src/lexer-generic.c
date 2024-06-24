#include <stdbool.h>

#include "common.h"
#include "types.h"

bool
skpcmnt(const uchar **ptr, const uchar *end)
{
	int nst = 1;
	const uchar *p = *ptr;

	for (p++; likely(p < end); p++) {
		if (p + 1 < end) {
			if (p[0] == '*' && p[1] == '/') {
				p++;
				if (--nst == 0) {
					*ptr = ++p;
					return true;
				}
			} else if (p[0] == '/' && p[1] == '*') {
				p++;
				nst++;
			}
		}
	}

	return false;
}
