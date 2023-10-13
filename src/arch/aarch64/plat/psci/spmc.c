/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/cpu.h"
#include "pg/dlog.h"

#include "psci.h"

void cpu_entry(struct cpu *c);

/**
 * Returns zero in context of the SPMC as it does not rely
 * on the EL3 PSCI framework.
 */
uint32_t plat_psci_version_get(void)
{
	return 0;
}

/**
 * Initialize the platform power managment module in context of
 * running the SPMC.
 */
void plat_psci_init(void)
{
	struct ffa_value res;

	/* Register SPMC secondary cold boot entry point */
	res = smc_ffa_call(
		(struct ffa_value){.func = FFA_SECONDARY_EP_REGISTER_64,
				   .arg1 = (uintreg_t)&cpu_entry});

	if (res.func != FFA_SUCCESS_64) {
		panic("FFA_SECONDARY_EP_REGISTER_64 failed");
	}
}

void plat_psci_cpu_suspend(uint32_t power_state)
{
	(void)power_state;
}

void plat_psci_cpu_resume(struct cpu *c, ipaddr_t entry_point)
{
	if (cpu_on(c, entry_point, 0UL)) {
		/*
		 * This is the boot time PSCI cold reset path (svc_cpu_on_finish
		 * handler relayed by SPMD) on secondary cores.
		 */
		dlog_verbose("%s: cpu mpidr 0x%x ON\n", __func__, c->id);
	}
}
