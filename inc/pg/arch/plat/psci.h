/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "pg/vcpu.h"

/**
 * Returns the platform specific PSCI version value.
 */
uint32_t plat_psci_version_get(void);

/**
 * Called once at boot time to initialize the platform power management module.
 */
void plat_psci_init(void);

/**
 * Called before the PSCI_CPU_SUSPEND SMC is forwarded. The power state is
 * provided to allow actions to be taken based on the implementation defined
 * meaning of this field.
 */
void plat_psci_cpu_suspend(uint32_t power_state);

/** Called when a CPU resumes from being off or suspended. */
void plat_psci_cpu_resume(struct cpu *c, ipaddr_t entry_point);
