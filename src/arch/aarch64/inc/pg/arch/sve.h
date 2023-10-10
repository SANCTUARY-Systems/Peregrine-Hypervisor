/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/types.h"

/** SVE vector size supported. */
#define PG_SVE_VECTOR_LENGTH 512

struct sve_context_t {
	uint8_t vectors[32][PG_SVE_VECTOR_LENGTH / 8];

	/* FFR and predicates are one-eigth of the SVE vector length */
	uint8_t ffr[PG_SVE_VECTOR_LENGTH / 64];

	uint8_t predicates[16][PG_SVE_VECTOR_LENGTH / 64];
} __attribute__((aligned(16)));
