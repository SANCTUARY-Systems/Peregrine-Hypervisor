/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "vmapi/pg/ffa.h"

/**
 * Called after an SMC has been forwarded. `args` contains the arguments passed
 * to the SMC and `ret` contains the return values that will be set in the vCPU
 * registers after this call returns.
 */
void plat_smc_post_forward(struct ffa_value args, struct ffa_value *ret);
