/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <stdint.h>

#include "pg/arch/types.h"

#include "pg/cpu.h"

bool psci_handler(struct vcpu *vcpu, uint32_t func, uintreg_t arg0,
		  uintreg_t arg1, uintreg_t arg2, uintreg_t *ret,
		  struct vcpu **next);
