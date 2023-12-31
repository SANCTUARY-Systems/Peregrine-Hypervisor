/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "pg/boot_params.h"
#include "pg/manifest.h"
#include "pg/memiter.h"
#include "pg/mm.h"

bool boot_flow_get_params(struct boot_params *p, const struct fdt *fdt);

bool boot_flow_update(struct mm_stage1_locked stage1_locked,
		      const struct manifest *manifest,
		      struct boot_params_update *p, struct memiter *cpio,
		      struct mpool *ppool);
