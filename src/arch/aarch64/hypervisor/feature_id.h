/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include "pg/arch/types.h"

#include "pg/cpu.h"

#include "vmapi/pg/ffa.h"

#define PG_FEATURE_NONE UINT64_C(0)

/*  Reliability, Availability, and Serviceability (RAS) Extension Features */
#define PG_FEATURE_RAS UINT64_C(1)

/* Limited Ordering Regions */
#define PG_FEATURE_LOR (UINT64_C(1) << 1)

/* Performance Monitor */
#define PG_FEATURE_PERFMON (UINT64_C(1) << 2)

/* Debug Registers */
#define PG_FEATURE_DEBUG (UINT64_C(1) << 3)

/* Statistical Profiling Extension (SPE) */
#define PG_FEATURE_SPE (UINT64_C(1) << 4)

/* Self-hosted Trace */
#define PG_FEATURE_TRACE (UINT64_C(1) << 5)

/* Pointer Authentication (PAuth) */
#define PG_FEATURE_PAUTH (UINT64_C(1) << 6)

/*
 * NOTE: This should be based on the last (highest value) defined feature.
 * Adjust if adding more features.
 */
#define PG_FEATURE_ALL ((PG_FEATURE_PAUTH << 1) - 1)

bool feature_id_is_register_access(uintreg_t esr_el2);

bool feature_id_process_access(struct vcpu *vcpu, uintreg_t esr_el2);

void feature_set_traps(struct vm *vm, struct arch_regs *regs);
