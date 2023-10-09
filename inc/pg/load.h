/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pg/boot_params.h"
#include "pg/cpio.h"
#include "pg/manifest.h"
#include "pg/memiter.h"
#include "pg/mm.h"
#include "pg/mpool.h"

void print_manifest(struct manifest_vm *manifest_vm, ffa_vm_id_t vm_id);

bool load_vms(struct mm_stage1_locked stage1_locked,
	      struct manifest *manifest, const struct memiter *cpio,
	      struct boot_params *params,
	      struct boot_params_update *update, struct mpool *ppool);

bool load_devices(struct mm_stage1_locked stage1_locked,
	      struct manifest_vm *manifest_vm, struct mpool *ppool);

/* PJ's load_ramdisk version / implementation in "src/load.c" */
//bool load_ramdisk(struct manifest_vm *manifest_vm, struct boot_params_update *update,
//		const struct memiter *cpio);
