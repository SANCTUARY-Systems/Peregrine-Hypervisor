/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

.section .init.image_entry, "ax"
.global image_entry
image_entry:
	/* Prepare the stack. */
	adr x30, kstack + 4096
	mov sp, x30

	/* Call into C code. */
	bl kmain

	/* Loop forever waiting for interrupts. */
0:	wfi
	b 0b
