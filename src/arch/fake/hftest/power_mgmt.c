/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/vm/power_mgmt.h"

#include <sys/reboot.h>

noreturn void arch_power_off(void)
{
	reboot(RB_POWER_OFF);
	for (;;) {
		/* This should never be reached. */
	}
}
