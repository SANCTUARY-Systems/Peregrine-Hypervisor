/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "pg/arch/types.h"

#include "pg/cpu.h"

#include "vmapi/pg/ffa.h"

bool debug_el1_is_register_access(uintreg_t esr_el2);

bool debug_el1_process_access(struct vcpu *vcpu, uint16_t vm_id,
			      uintreg_t esr_el2);
