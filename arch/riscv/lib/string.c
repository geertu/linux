// SPDX-License-Identifier: GPL-2.0-only
/*
 * String functions optimized for hardware which doesn't
 * handle unaligned memory accesses efficiently.
 *
 * Copyright (C) 2021 Matteo Croce
 */

#include <linux/types.h>
#include <linux/export.h>

/* Minimum size for a word copy to be convenient */
#define MIN_THRESHOLD (BITS_PER_LONG / 8 * 2)

/* convenience union to avoid cast between different pointer types */
union types {
	u8 *as_u8;
	unsigned long *as_ulong;
	uintptr_t as_uptr;
};

union const_types {
	const u8 *as_u8;
	const unsigned long *as_ulong;
	uintptr_t as_uptr;
};

static const unsigned int bytes_long = sizeof(long);
static const unsigned int mask = bytes_long - 1;

void *__memcpy(void *dest, const void *src, size_t count)
{
	union const_types s = { .as_u8 = src };
	union types d = { .as_u8 = dest };
	int distance = 0;

	if (count < MIN_THRESHOLD)
		goto copy_remainder;

	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
		/* Copy a byte at time until destination is aligned. */
		for (; d.as_uptr & mask; count--)
			*d.as_u8++ = *s.as_u8++;

		distance = s.as_uptr & mask;
	}

	if (distance) {
		unsigned long last, next;

		/*
		 * s is distance bytes ahead of d, and d just reached
		 * the alignment boundary. Move s backward to word align it
		 * and shift data to compensate for distance, in order to do
		 * word-by-word copy.
		 */
		s.as_u8 -= distance;

		/*
		 * Word-by-word copy by shifting data.
		 * Works only on Little Endian machines.
		 */
		next = s.as_ulong[0];
		for (; count >= bytes_long + mask; count -= bytes_long) {
			last = next;
			next = s.as_ulong[1];

			d.as_ulong[0] = last >> (distance * 8) |
					next << ((bytes_long - distance) * 8);

			d.as_ulong++;
			s.as_ulong++;
		}

		/* Restore s with the original offset. */
		s.as_u8 += distance;
	} else {
		/*
		 * If the source and dest lower bits are the same, do a simple
		 * 32/64 bit wide copy.
		 */
		for (; count >= bytes_long; count -= bytes_long)
			*d.as_ulong++ = *s.as_ulong++;
	}

copy_remainder:
	while (count--)
		*d.as_u8++ = *s.as_u8++;

	return dest;
}
EXPORT_SYMBOL(__memcpy);

void *memcpy(void *dest, const void *src, size_t count) __weak __alias(__memcpy);
EXPORT_SYMBOL(memcpy);

/*
 * Simply check if the buffer overlaps an call memcpy() in case,
 * otherwise do a simple one byte at time backward copy.
 */
void *__memmove(void *dest, const void *src, size_t count)
{
	if (dest < src || src + count <= dest)
		return memcpy(dest, src, count);

	if (dest > src) {
		const char *s = src + count;
		char *tmp = dest + count;

		while (count--)
			*--tmp = *--s;
	}
	return dest;
}
EXPORT_SYMBOL(__memmove);

void *memmove(void *dest, const void *src, size_t count) __weak __alias(__memmove);
EXPORT_SYMBOL(memmove);

void *__memset(void *s, int c, size_t count)
{
	union types dest = { .as_u8 = s };

	if (count >= MIN_THRESHOLD) {
		unsigned long cu = (unsigned long)c;

		/* Compose an ulong with 'c' repeated 4/8 times */
		cu |= cu << 8;
		cu |= cu << 16;
#if BITS_PER_LONG == 64
		cu |= cu << 32;
#endif

		if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)) {
			/*
			 * Fill the buffer one byte at time until
			 * the destination is word aligned.
			 */
			for (; count && dest.as_uptr & mask; count--)
				*dest.as_u8++ = c;
		}

		/* Copy using the largest size allowed */
		for (; count >= bytes_long; count -= bytes_long)
			*dest.as_ulong++ = cu;
	}

	/* copy the remainder */
	while (count--)
		*dest.as_u8++ = c;

	return s;
}
EXPORT_SYMBOL(__memset);

void *memset(void *s, int c, size_t count) __weak __alias(__memset);
EXPORT_SYMBOL(memset);
