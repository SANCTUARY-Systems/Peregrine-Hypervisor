/*
 * Copyright 2020 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <stdalign.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "pg/arch/std.h"

#include "pg/addr.h"
#include "pg/dlog.h"
#include "pg/layout.h"
#include "pg/panic.h"

#include "fwcfg.h"
#include "libfdt.h"

#define FDT_MAX_SIZE 0x10000

alignas(4096) uint8_t kstack[4096];

typedef void entry_point(struct fdt_header *, uint64_t, uint64_t, uint64_t);

static noreturn void jump_to_kernel(struct fdt_header *fdt,
				    uintptr_t kernel_start)
{
	entry_point *kernel_entry = (entry_point *)kernel_start;

	kernel_entry(fdt, 0, 0, 0);

	/* This should never be reached. */
	for (;;) {
	}
}

static bool update_fdt(struct fdt_header *fdt, uintptr_t initrd_start,
		       uint32_t initrd_size)
{
	uintptr_t initrd_end = initrd_start + initrd_size;
	int ret;
	int chosen_offset;

	ret = fdt_check_header(fdt);
	if (ret != 0) {
		dlog_error("FDT failed validation: %d\n", ret);
		return false;
	}
	ret = fdt_open_into(fdt, fdt, FDT_MAX_SIZE);
	if (ret != 0) {
		dlog_error("FDT failed to open: %d\n", ret);
		return false;
	}

	chosen_offset = fdt_path_offset(fdt, "/chosen");
	if (chosen_offset <= 0) {
		dlog_error("Unable to find '/chosen'\n");
		return false;
	}

	/* Patch FDT to point to new ramdisk. */
	ret = fdt_setprop_u64(fdt, chosen_offset, "linux,initrd-start",
			      initrd_start);
	if (ret != 0) {
		dlog_error("Unable to write linux,initrd-start: %d\n", ret);
		return false;
	}

	ret = fdt_setprop_u64(fdt, chosen_offset, "linux,initrd-end",
			      initrd_end);
	if (ret != 0) {
		dlog_error("Unable to write linux,initrd-end\n");
		return false;
	}

	ret = fdt_pack(fdt);
	if (ret != 0) {
		dlog_error("Failed to pack FDT.\n");
		return false;
	}

	return true;
}

noreturn void kmain(struct fdt_header *fdt)
{
	uintptr_t kernel_start;
	uint32_t kernel_size;

	/* Load the initrd just after this bootloader. */
	paddr_t image_end = layout_image_end();
	uintptr_t initrd_start = align_up(pa_addr(image_end), LINUX_ALIGNMENT);
	uint32_t initrd_size = fw_cfg_read_uint32(FW_CFG_INITRD_SIZE);

	dlog_debug("Initrd start %#x, size %#x\n", initrd_start, initrd_size);
	fw_cfg_read_bytes(FW_CFG_INITRD_DATA, initrd_start, initrd_size);

	/*
	 * Load the kernel after the initrd. Follow Linux alignment conventions
	 * just in case.
	 */
	kernel_start = align_up(initrd_start + initrd_size, LINUX_ALIGNMENT) +
		       LINUX_OFFSET;
	kernel_size = fw_cfg_read_uint32(FW_CFG_KERNEL_SIZE);
	dlog_debug("Kernel start %#x, size %#x\n", kernel_start, kernel_size);
	fw_cfg_read_bytes(FW_CFG_KERNEL_DATA, kernel_start, kernel_size);

	/* Update FDT to point to initrd. */
	if (initrd_size > 0) {
		if (update_fdt(fdt, initrd_start, initrd_size)) {
			dlog_debug("Updated FDT with initrd.\n");
		} else {
			panic("Failed to update FDT.");
		}
	}

	/* Jump to the kernel. */
	jump_to_kernel(fdt, kernel_start);
}
