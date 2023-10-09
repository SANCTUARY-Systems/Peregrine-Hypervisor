/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include "pg/arch/std.h"
#include <stdatomic.h>
#include <stdbool.h>

#define STRINGIFY(X) STRINGIFY2(X)    
#define STRINGIFY2(X) #X

typedef size_t rsize_t;

/**
 * Restrict the maximum range for range checked functions so as to be more
 * likely to catch errors. This may need to be relaxed if it proves to be overly
 * restrictive.
 */
#define RSIZE_MAX (128 * 1024 * 1024)

/* Helper Functions*/
void uint64_to_uint64_pair(uint64_t *dest_high, uint64_t *dest_low, uint64_t src);

uint64_t uint64_pair_to_uint64(uint64_t *src_high, uint64_t *src_low);

bool atomic_inc(atomic_int *val, int less_than);

bool __atomic_dec(atomic_int *val, int less_than);

#define atomic_dec(val) __atomic_dec(val, 0)

/*
 * Only the safer versions of these functions are exposed to reduce the chance
 * of misusing the versions without bounds checking or null pointer checks.
 *
 * These functions don't return errno_t as per the specification and implicitly
 * have a constraint handler that panics.
 */
void memset_s(void *dest, rsize_t destsz, int ch, rsize_t count);
void memcpy_s(void *dest, rsize_t destsz, const void *src, rsize_t count);
void memmove_s(void *dest, rsize_t destsz, const void *src, rsize_t count);

void memset_unsafe(void *dest, int ch, rsize_t count);
void memcpy_unsafe(void *dest, const void *src, rsize_t count);
void memmove_unsafe(void *dest, const void *src, rsize_t count);
size_t strnlen_s(const char *str, size_t strsz);
