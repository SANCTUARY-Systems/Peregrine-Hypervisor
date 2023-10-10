/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/plat/smc.h"

void plat_smc_post_forward(struct ffa_value args, struct ffa_value *ret)
{
	(void)args;
	(void)ret;
}
