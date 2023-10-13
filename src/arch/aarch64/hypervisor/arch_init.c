/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/plat/psci.h"

/**
 * Performs arch specific boot time initialization.
 */
void arch_one_time_init(void)
{
	plat_psci_init();
}
