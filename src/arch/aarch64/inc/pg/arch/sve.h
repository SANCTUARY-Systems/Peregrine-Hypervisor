/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/types.h"

/** SVE vector size supported. */
#define PG_SVE_VECTOR_LENGTH 512

struct sve_context_t {
	uint8_t vectors[32][PG_SVE_VECTOR_LENGTH / 8];

	/* FFR and predicates are one-eigth of the SVE vector length */
	uint8_t ffr[PG_SVE_VECTOR_LENGTH / 64];

	uint8_t predicates[16][PG_SVE_VECTOR_LENGTH / 64];
} __attribute__((aligned(16)));
