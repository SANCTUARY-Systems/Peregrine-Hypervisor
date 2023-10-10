/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

.global vm_cpu_entry
vm_cpu_entry:
	/* Disable trapping floating point access in EL1. */
	mov x1, #(0x3 << 20)
	msr cpacr_el1, x1
	isb

	/* Initialise stack from the cpu_start_state struct. */
	ldr x1, [x0]
	mov sp, x1

	/* Load entry function pointer and its argument. */
	ldr x1, [x0, 8]
	ldr x0, [x0, 16]

	/* Branch to entry function. */
	blr x1

	/* Entry function should not return, but if it does, spin. */
	b .
