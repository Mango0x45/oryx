#ifndef ORYX_BITSET_H
#define ORYX_BITSET_H

#include <limits.h>
#include <stdbool.h>

#include "alloc.h"
#include "common.h"

typedef unsigned char bitset_t;

#define _BITSET_BITS (sizeof(bitset_t) * CHAR_BIT)
#define _BITSLOT(x)  ((x) / _BITSET_BITS)
#define _BITMASK(x)  (1 << ((x) % _BITSET_BITS))

/* Set- or test the bit as position X in BS */
#define SETBIT(bs, x)  ((bs)[_BITSLOT(x)] |= _BITMASK(x))
#define TESTBIT(bs, x) ((bool)((bs)[_BITSLOT(x)] & _BITMASK(x)))

/* Set the bit DST in BS if the bit SRC is set */
#define SET_DST_IF_SRC(bs, dst, src)                                           \
	do {                                                                       \
		if (TESTBIT(bs, src))                                                  \
			SETBIT(bs, dst);                                                   \
	} while (false)

/* Allocate a new bitset capable of holding N bits */
static inline bitset_t *mkbitset(arena_t *, size_t n)
	__attribute__((always_inline, returns_nonnull, nonnull, malloc));

bitset_t *
mkbitset(arena_t *a, size_t n)
{
	size_t bits = CHAR_BIT * sizeof(bitset_t);
	return arena_new(a, bitset_t, (n + bits - 1) / bits);
}

#endif /* !ORYX_BITSET_H */
