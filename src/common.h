#ifndef ORYX_COMMON_H
#define ORYX_COMMON_H

#ifdef __GNUC__
#	define likely(x)   __builtin_expect(!!(x), 1)
#	define unlikely(x) __builtin_expect(!!(x), 0)
#else
#	include <stdlib.h>
#	define __attribute__(x)
#	define __builtin_unreachable() abort()
#	define likely(x)               (x)
#	define unlikely(x)             (x)
#endif

/* Make printf() always available in debug builds */
#ifdef DEBUG
#	include <stdio.h>
#endif

#define MPQ_IS_INIT(x) (mpq_denref(x)->_mp_d != NULL)

#endif /* !ORYX_COMMON_H */
