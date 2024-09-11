/*
 * Copyright (c) 2023 SANCTUARY Systems GmbH
 *
 * This file is free software: you may copy, redistribute and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * For a commercial license, please contact SANCTUARY Systems GmbH
 * directly at info@sanctuary.dev
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *     Copyright 2019 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/check.h"
#include "pg/cpio.h"
#include "pg/dlog.h"
#include "pg/fdt_handler.h"
#include "pg/plat/boot_flow.h"
#include "pg/std.h"
#include "pg/layout.h"
#include "pg/load.h"
#include "pg/pma.h"

/* Set by arch-specific boot-time hook. */
uintreg_t plat_boot_flow_fdt_addr;

/**
 * Returns the physical address of board FDT. This was passed to Peregrine in the
 * first kernel arg by the boot loader.
 */
paddr_t plat_boot_flow_get_fdt_addr(void)
{
	dlog_debug("plat_boot_flow_fdt_addr: %#x\n", plat_boot_flow_fdt_addr);
	return pa_init((uintpaddr_t)plat_boot_flow_fdt_addr);
}

/**
 * When handing over to the primary, give it the same FDT address that was given
 * to Peregrine. The FDT may have been modified during Peregrine init.
 */
uintreg_t plat_boot_flow_get_kernel_arg(void)
{

	dlog_debug("plat_boot_flow_fdt_addr: %#x\n", plat_boot_flow_fdt_addr);
	return plat_boot_flow_fdt_addr;
}

/**
 * Load initrd range from the board FDT.
 */
bool plat_boot_flow_get_initrd_range(const struct fdt *fdt, paddr_t *begin,
				     paddr_t *end)
{
	return fdt_find_initrd(fdt, begin, end);
}

bool plat_boot_flow_update(struct mm_stage1_locked stage1_locked,
			   const struct manifest *manifest,
			   struct boot_params_update *update,
			   struct memiter *cpio, struct mpool *ppool)
{
	struct memiter primary_initrd;
	const struct string *filename =
		&manifest->vm[PG_PRIMARY_VM_INDEX].ramdisk_filename;

	if (string_is_empty(filename)) {
		memiter_init(&primary_initrd, NULL, 0);
	} else if (!cpio_get_file(cpio, filename, &primary_initrd)) {
		dlog_error("Unable to find primary initrd \"%s\".\n",
			   string_data(filename));
		return false;
	}

	//TODO: only use this ramdisk memory rang if the ramdisk was loaded to it...
	update->initrd_begin.pa = pa_addr(manifest->vm[PG_PRIMARY_VM_INDEX].ramdisk_addr_pa);
	update->initrd_end.pa = pa_addr(manifest->vm[PG_PRIMARY_VM_INDEX].ramdisk_addr_pa) + manifest->vm[PG_PRIMARY_VM_INDEX].ramdisk_size;

	return true;
}
