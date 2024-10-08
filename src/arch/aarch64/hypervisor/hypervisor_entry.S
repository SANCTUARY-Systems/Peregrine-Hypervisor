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
 *     Copyright 2018 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/offsets.h"
#include "pg/cpu.h"
#include "msr.h"

.macro get_core_affinity reg1 reg2
	mrs \reg1, mpidr_el1
	ubfx \reg2, \reg1, 0, 24
	ubfx \reg1, \reg1, 32, 8
	orr \reg1, \reg2, \reg1, lsl #32
.endm

/** TODO Check Hafnium's MTE support **/

/**
 * Called only on first boot after the image has been relocated and BSS zeroed.
 *
 * It is required that caches be clean and invalid.
 */
.global __stack_chk_guard

.section .init.image_entry, "ax"
.global image_entry
image_entry:
	/* Interpret the registers passed from the loader. */
	bl plat_boot_flow_hook

	/* Get pointer to first CPU. */
	adrp x28, cpus
	add x28, x28, :lo12:cpus

	/* Set the ID of this CPU from the affinity bits of mpidr. */
	get_core_affinity x30, x29
	str x30, [x28, CPU_ID]

	mov x0, x28
	bl prepare_for_c

	/* Set stack protector */
	adrp x19, __stack_chk_guard
	bl __stack_chk_guard_setup
	mov x20, x0
	str x20, [x19, #:lo12:__stack_chk_guard]

	/*
	 * Call into C to initialize the memory management configuration with
	 * MMU and caches disabled. Result will be stored in `arch_mm_config`.
	 */
	bl one_time_init_mm

#if BRANCH_PROTECTION
	/* Expect pointer authentication is implemented. */
	mrs	x1, id_aa64isar1_el1
	ands	x1, x1, #0xff0		/* API / APA */
	beq	.

	/* Gather a random number to use as pointer authentication key. */
	bl	plat_prng_get_number
	adrp	x3, pauth_apia_key
	add	x2, x3, :lo12: pauth_apia_key
	stp	x0, x1, [x2]
#endif

	/* Enable MMU and caches before running the rest of initialization. */
	bl mm_enable
	bl one_time_init

	/* Begin steady state operation. */
	mov x0, x28
	b cpu_init

/**
 * Entry point for all cases other than the first boot e.g. secondary CPUs and
 * resuming from suspend.
 *
 * It is required that caches be coherent but not necessarily clean or invalid.
 *
 * x0 points to the current CPU.
 */
.section .text.entry, "ax"
.global cpu_entry
cpu_entry:

#if BRANCH_PROTECTION
	/* Expect pointer authentication is implemented. */
	mrs	x1, id_aa64isar1_el1
	ands	x1, x1, #0xff0		/* API / APA */
	beq	.
#endif

	bl mm_enable
	bl prepare_for_c

	/* Intentional fallthrough. */

cpu_init:
	/* Call into C code, x0 holds the CPU pointer. */
	bl cpu_main

	/* Run the vCPU returned by cpu_main. */
	bl vcpu_restore_all_and_run

	/* Loop forever waiting for interrupts. */
0:	wfi
	b 0b

/**
 * Set up CPU environment for executing C code. This is called on first boot
 * with caches disabled but subsequent calls will have caches enabled.
 *
 * x0 points to the current CPU on entry and exit.
 */
prepare_for_c:
	/* Use SPx (instead of SP0). */
	msr spsel, #1

	/* Prepare the stack. */
	ldr x1, [x0, #CPU_STACK_BOTTOM]
	mov sp, x1

	/* Configure exception handlers. */
	adr x2, vector_table_el2
	msr vbar_el2, x2
	ret

/**
 * Applies the memory management configuration to the CPU, preserving x0 along
 * the way.
 */
mm_enable:
	/*
	 * Invalidate any potentially stale local TLB entries for the
	 * hypervisor's stage-1 and the VM's stage-2 before they start being
	 * used. The VM's stage-1 is invalidated as a side effect but it wasn't
	 * using it yet anyway.
	 */
	tlbi alle2
	tlbi vmalls12e1

	/*
	 * Load and apply the memory management configuration. Order depends on
	 * `struct arch_mm_config.
	 */
	adrp x7, arch_mm_config
	add x7, x7, :lo12:arch_mm_config

	ldp x1, x2, [x7]
	ldp x3, x4, [x7, #16]
	ldp x5, x6, [x7, #32]
	ldr x7, [x7, #48]

	/*
	 * Set hcr_el2 before tcr_el2, since hcr_el2.e2h may be set, which changes
	 * the definition of tcr_el2.
	 */
	msr hcr_el2, x7
	isb

	msr ttbr0_el2, x1
	msr vtcr_el2, x2

	msr mair_el2, x3
	msr tcr_el2, x4

	/* Ensure everything before this point has completed. */
	dsb sy
	isb

#if BRANCH_PROTECTION
	/* Load EL2 APIA Key. */
	adrp	x1, pauth_apia_key
	add	x1, x1, :lo12: pauth_apia_key
	ldp	x1, x2, [x1]
	msr     APIAKEYLO_EL1, x1
	msr     APIAKEYHI_EL1, x2
#endif

	/*
	 * Configure sctlr_el2 to enable MMU and cache and don't proceed until
	 * this has completed.
	 */
	msr sctlr_el2, x5
	isb
	ret
