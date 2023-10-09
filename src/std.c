/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/std.h"
#include "pg/arch/std.h"
#include "pg/check.h"

/* Declare unsafe functions locally so they are not available globally. */
#if 0
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
#endif


void uint64_to_uint64_pair(uint64_t *dest_high, uint64_t *dest_low, uint64_t src)
{
	*dest_high = src >> 32;
	*dest_low = (uint32_t)src;
}

uint64_t uint64_pair_to_uint64(uint64_t *src_high, uint64_t *src_low)
{
	return (*src_high << 32 | (uint32_t)*src_low);
}

/* TODO: move these two atomic functions to a more appropriate file */
bool atomic_inc(atomic_int *val, int less_than)
{
	int new_val;
	int old_val = atomic_load(val);
	do {
		if (old_val > less_than) {
			return false;
		}
		new_val = old_val + 1;
	} while (!atomic_compare_exchange_weak(val, &old_val, new_val));

	return true;
}

bool __atomic_dec(atomic_int *val, int less_than)
{
	int new_val;
	int old_val = atomic_load(val);
	do {
		if (old_val <= less_than) {
			return false;
		}
		new_val = old_val - 1;
	} while (!atomic_compare_exchange_weak(val, &old_val, new_val));

	return true;
}


/*
 * As per the C11 specification, mem*_s() operations fill the destination buffer
 * if runtime constraint validation fails, assuming that `dest` and `destsz`
 * are both valid.
 */
#define CHECK_OR_FILL(cond, dest, destsz, ch)                               \
	do {                                                                \
		if (!(cond)) {                                              \
			if ((dest) != NULL && (destsz) <= RSIZE_MAX) {      \
				memset_s((dest), (destsz), (ch), (destsz)); \
			}                                                   \
			panic("%s failed: " #cond, __func__);               \
		}                                                           \
	} while (0)

#define CHECK_OR_ZERO_FILL(cond, dest, destsz) \
	CHECK_OR_FILL(cond, dest, destsz, '\0')

void memset_s(void *dest, rsize_t destsz, int ch, rsize_t count)
{
	if (dest == NULL || destsz > RSIZE_MAX) {
		panic("memset_s failed as either dest == NULL "
		      "or destsz > RSIZE_MAX.\n");
	}

	/*
	 * Clang analyzer doesn't like us calling unsafe memory functions, so
	 * make it ignore this call.
	 */

	// NOLINTNEXTLINE
	memset_peregrine(dest, ch, count);
}

void memcpy_s(void *dest, rsize_t destsz, const void *src, rsize_t count)
{
	uintptr_t d = (uintptr_t)dest;
	uintptr_t s = (uintptr_t)src;

	CHECK_OR_ZERO_FILL(dest != NULL, dest, destsz);
	CHECK_OR_ZERO_FILL(src != NULL, dest, destsz);

	/* Check count <= destsz <= RSIZE_MAX. */
	CHECK_OR_ZERO_FILL(destsz <= RSIZE_MAX, dest, destsz);
	CHECK_OR_ZERO_FILL(count <= destsz, dest, destsz);

	/*
	 * Buffer overlap test.
	 * case a) `d < s` implies `s >= d+count`
	 * case b) `d > s` implies `d >= s+count`
	 */
	CHECK_OR_ZERO_FILL(d != s, dest, destsz);
	CHECK_OR_ZERO_FILL(d < s || d >= (s + count), dest, destsz);
	CHECK_OR_ZERO_FILL(d > s || s >= (d + count), dest, destsz);

	// NOLINTNEXTLINE
	memcpy_peregrine(dest, src, count);
}

void memmove_s(void *dest, rsize_t destsz, const void *src, rsize_t count)
{
	CHECK_OR_ZERO_FILL(dest != NULL, dest, destsz);
	CHECK_OR_ZERO_FILL(src != NULL, dest, destsz);

	/* Check count <= destsz <= RSIZE_MAX. */
	CHECK_OR_ZERO_FILL(destsz <= RSIZE_MAX, dest, destsz);
	CHECK_OR_ZERO_FILL(count <= destsz, dest, destsz);

	// NOLINTNEXTLINE
	memmove_peregrine(dest, src, count);
}

/**
 * Returns the length of the null-terminated byte string `str`, examining at
 * most `strsz` bytes.
 *
 * If `str` is a NULL pointer, it returns zero.
 * If a NULL character is not found, it returns `strsz`.
 */
size_t strnlen_s(const char *str, size_t strsz)
{
	if (str == NULL) {
		return 0;
	}

	for (size_t i = 0; i < strsz; ++i) {
		if (str[i] == '\0') {
			return i;
		}
	}

	/* NULL character not found. */
	return strsz;
}

void memset_unsafe(void *dest, int ch, rsize_t count) {
	memset_peregrine(dest, ch, count);
}

void memcpy_unsafe(void *dest, const void *src, rsize_t count) {
	memcpy_peregrine(dest, src, count);
}

void memmove_unsafe(void *dest, const void *src, rsize_t count) {
	memmove_peregrine(dest, src, count);
}
