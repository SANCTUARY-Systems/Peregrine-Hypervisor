/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "pg/arch/types.h"

enum power_status {
	POWER_STATUS_ON,
	POWER_STATUS_OFF,
	POWER_STATUS_ON_PENDING,
};

/**
 * Holds temporary state used to set up the environment on which CPUs will
 * start executing.
 *
 * vm_cpu_entry() depends on the layout of this struct.
 */
struct arch_cpu_start_state {
	uintptr_t initial_sp;
	void (*entry)(uintreg_t arg);
	uintreg_t arg;
};

bool arch_cpu_start(uintptr_t id, struct arch_cpu_start_state *s);

noreturn void arch_cpu_stop(void);
enum power_status arch_cpu_status(cpu_id_t cpu_id);

noreturn void arch_power_off(void);
noreturn void arch_reboot(void);
