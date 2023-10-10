/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include "pg/addr.h"
#include "pg/boot_params.h"
#include "pg/fdt.h"
#include "pg/manifest.h"
#include "pg/memiter.h"
#include "pg/mm.h"

paddr_t plat_boot_flow_get_fdt_addr(void);
uintreg_t plat_boot_flow_get_kernel_arg(void);
bool plat_boot_flow_get_initrd_range(const struct fdt *fdt, paddr_t *begin,
				     paddr_t *end);
bool plat_boot_flow_update(struct mm_stage1_locked stage1_locked,
			   const struct manifest *manifest,
			   struct boot_params_update *p, struct memiter *cpio,
			   struct mpool *ppool);
