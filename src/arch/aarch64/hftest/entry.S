/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

.section .init.image_entry, "ax"
.global image_entry
image_entry:
	/* Prepare the stack. */
	adrp x30, kstack
	add sp, x30, #4096

	/* Disable trapping floating point access in EL1. */
	mov x30, #(0x3 << 20)
	msr cpacr_el1, x30
	isb

	/* Call into C code. */
	bl kmain

	/* If the VM returns, shutdown the system. */
	bl arch_power_off

	/* Loop forever waiting for interrupts. */
0:	wfi
	b 0b
