#ifndef ORYX_COMMON_H
#define ORYX_COMMON_H

#ifdef __GNUC__
#	define likely(x)   __builtin_expect(!!(x), 1)
#	define unlikely(x) __builtin_expect(!!(x), 0)
#else
#	define __attribute__(x)
#	define __builtin_unreachable() (((char *)0)[0] = 0)
#	define likely(x)               (x)
#	define unlikely(x)             (x)
#endif

#endif /* !ORYX_COMMON_H */
