/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/init.h"

#include <stdalign.h>
#include <stddef.h>

#include "../inc/pg/pma.h"

#include "pg/api.h"
#include "pg/boot_flow.h"
#include "pg/boot_params.h"
#include "pg/cpio.h"
#include "pg/cpu.h"
#include "pg/dlog.h"
#include "pg/fdt_handler.h"
#include "pg/load.h"
#include "pg/mm.h"
#include "pg/mpool.h"
#include "pg/panic.h"
#include "pg/plat/boot_flow.h"
#include "pg/plat/console.h"
#include "pg/plat/iommu.h"
#include "pg/std.h"
#include "pg/vm.h"
#include "pg/cpu.h"
#include "pg/load.h"

#include "psci.h"

#include "pg/arch/tee/mediator.h"
#include "pg/arch/tee/default_mediator.h"

/* Include macro for external mediators */
#ifdef EXTERNAL_MEDIATOR_INCLUDE
#  include STRINGIFY(EXTERNAL_MEDIATOR_INCLUDE)
#endif

#include "vmapi/pg/call.h"

#include <smc.h>
#define PSCI_CPU_ON_AARCH64		0xc4000003


alignas(MM_PPOOL_ENTRY_SIZE) char ptable_buf[MM_PPOOL_ENTRY_SIZE * HEAP_PAGES];

static alignas(MM_PPOOL_ENTRY_SIZE) struct manifest manifest_raw;

struct mpool ppool;

/* get_ppool - reference getter for ppool
 *  @return : address of ppool
 *
 * Needed when mapping devices to VM address spaces (on demand).
 */
struct mpool *
get_ppool(void)
{
    return &ppool;
}

void cpu_entry(struct cpu *c);

/**
 * Performs one-time initialisation of memory management for the hypervisor.
 *
 * This is the only C code entry point called with MMU and caching disabled. The
 * page table returned is used to set up the MMU and caches for all subsequent
 * code.
 */
void one_time_init_mm(void)
{
	/* Make sure the console is initialised before calling dlog. */
	plat_console_init();

	dlog_info("Initializing Peregrine Hypervisor\n");

	mpool_init(&ppool, MM_PPOOL_ENTRY_SIZE);
	mpool_add_chunk(&ppool, ptable_buf, sizeof(ptable_buf));

	if (!mm_init(&ppool)) {
		panic("mm_init failed");
	}
}

/**
 * Performs one-time initialisation of the hypervisor.
 */
void one_time_init(void)
{
	struct string manifest_fname = STRING_INIT("manifest.dtb");
	struct memiter manifest_it;
	struct memiter manifest_sig_it;

#if MEASURED_BOOT
	struct string manifest_sig_fname = STRING_INIT("manifest_signature.sig");
#endif
	struct fdt fdt;
	
	struct manifest *manifest = &manifest_raw;

	struct boot_params params;
	struct boot_params_update update;
	struct memiter cpio;

	void *initrd;
	size_t i;
	struct mm_stage1_locked mm_stage1_locked;
	bool sw_enabled = false;

	arch_one_time_init();

	/* Enable locks now that mm is initialised. */
	dlog_enable_lock();
	mpool_enable_locks();

	mm_stage1_locked = mm_lock_stage1();

	if (!fdt_map(&fdt, mm_stage1_locked, plat_boot_flow_get_fdt_addr(),
		     &ppool)) {
		panic("Unable to map FDT.");
	}
	dlog_debug("fdt_address: %#x\n", plat_boot_flow_get_fdt_addr);


	if (!boot_flow_get_params(&params, &fdt)) {
		panic("Could not parse boot params.");
	}

	/* Initialize physical CPU structure */
	if (params.cpu_count > MAX_CPUS) {
		panic("Found more than %d CPUs\n", MAX_CPUS);
	}

	for (i = 0; i < params.mem_ranges_count; ++i) {
		dlog_debug("Memory range:  %#x - %#x\n",
			  pa_addr(params.mem_ranges[i].begin),
			  pa_addr(params.mem_ranges[i].end) - 1);
	}


	if (!pa_addr(params.initrd_begin)) {
		panic("No Ramdisk!");
	}
	dlog_debug("Ramdisk range: %#x - %#x\n",
		  pa_addr(params.initrd_begin),
		  pa_addr(params.initrd_end) - 1);

	/* Map initrd in, and initialise cpio parser. */
	initrd = mm_identity_map_and_reserve(mm_stage1_locked, params.initrd_begin,
				 params.initrd_end, MM_MODE_R, &ppool);
	if (!initrd) {
		panic("Unable to map initrd.");
	}

	memiter_init(
		&cpio, initrd,
		pa_difference(params.initrd_begin, params.initrd_end));

	/* Get manifest binary */
	if (!cpio_get_file(&cpio, &manifest_fname, &manifest_it)) {
		panic("Could not find manifest in initrd.");
	}

#if MEASURED_BOOT
	/* Get manifest signature */
	if (!cpio_get_file(&cpio, &manifest_sig_fname, &manifest_sig_it)) {
		panic("Could not find manifest signature in initrd.");
	}
#endif
	dlog_info("Manifest range: %#x - %#x (%d bytes)\n", manifest_it.next,
				manifest_it.limit, manifest_it.limit - manifest_it.next);

	if (!is_aligned(manifest_it.next, 4)) {
		panic("Manifest not aligned.");
	}

	/* Register external mediator - defaults to false if none available */
	if (!register_external_mediator(&fdt)) {
		dlog_info("Registering default mediator.\n");
		register_default_mediator();
	}

	/* Call mediator probe after initialzation to populate manifest */
	if (!tee_mediator_ops.probe(mm_stage1_locked, &ppool, &manifest_it, &manifest_sig_it, &manifest)) {
		dlog_error("Could not probe mediator. (Re-) Loading default mediator...\n");
		unregister_mediator();
		register_default_mediator();
		if (!tee_mediator_ops.probe(mm_stage1_locked, &ppool, &manifest_it, &manifest_sig_it, &manifest))
			panic("Could not parse manifest.");
	} else {
		sw_enabled = true;
	}

	if (!plat_iommu_init(&fdt, mm_stage1_locked, &ppool)) {
		panic("Could not initialize IOMMUs.");
	}

	if (!fdt_unmap(&fdt, mm_stage1_locked, &ppool)) {
		panic("Unable to unmap FDT.");
	}

	cpu_module_init(params.cpu_ids, params.cpu_count);

	/* Load all VMs. */
	update.reserved_ranges_count = 0;
	if (!load_vms(mm_stage1_locked, manifest, &cpio, &params, &update,
		      &ppool)) {
		panic("Unable to load VMs.");
	}

	if (sw_enabled) {
		for (i = 0; i < manifest->vm_count; i++) {
			/* Call per-VM init function of the mediator */
			int ret = tee_mediator_ops.vm_init(i, &manifest_it, &manifest->vm[i]);
			if (ret != 0x0) {
				panic("[VM %u] verification failed. Aborting.", (uint16_t) (i + 1));
			}
		}
	} else {
		dlog_warning("VMs could not be verified, no TOS found.\n");
	}

	/* Load devices of all VMs */
	for (i = 0; i < manifest->vm_count; i++) {
		if (!load_devices(mm_stage1_locked, &manifest->vm[i], &ppool)) {
			panic("[VM %u] assignment of devices failed. Aborting.", (uint16_t) (i + 1));
		}
	}

	#if LOG_LEVEL >= LOG_LEVEL_DEBUG
	for (i = 0; i < manifest->vm_count; i++) {
		print_manifest(&manifest->vm[i], i);
	}
	#endif

	if (!boot_flow_update(mm_stage1_locked, manifest, &update, &cpio,
			      &ppool)) {
		panic("Unable to update boot flow.");
	}

	mm_unlock_stage1(&mm_stage1_locked);

	/* Enable TLB invalidation for VM page table updates. */
	mm_vm_enable_invalidation();

	dlog_info("Peregrine initialisation completed\n");
	dlog_debug("VM count: %u\n", vm_get_count());

	/* Boot all secondary VMs in reverse order */
	uint32_t smc_fid = PSCI_CPU_ON;
	uintptr_t caller_id = HYPERVISOR_ID;
	uintptr_t entrypoint = (uintreg_t)&cpu_entry;
	struct vm* vm;
	cpu_id_t vm_primary_core;
	struct cpu *c;

	for (int i = vm_get_count()-1; i > 0; i--) {
		vm = vm_find_index(i);
		vm_primary_core = vm->cpus[0];
		c = cpu_find(vm_primary_core);

		dlog_info("Starting VM 0x%x by booting vCPU 0x0 on CPU 0x%x\n", vm->id, c->id);
		smc64(smc_fid, c->id, entrypoint, (uintreg_t)c, 0, 0, 0, caller_id);
	}
}
