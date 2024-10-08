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

bool debug_el1_is_register_access(uintreg_t esr_el2);

bool debug_el1_process_access(struct vcpu *vcpu, uint16_t vm_id,
			      uintreg_t esr_el2);
