/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include "pg/mm.h"

bool arch_vm_mm_init(void);
void arch_vm_mm_enable(paddr_t table);

/**
 * Reset MMU-related system registers. Must be called after arch_vm_mm_init().
 */
void arch_vm_mm_reset(void);
