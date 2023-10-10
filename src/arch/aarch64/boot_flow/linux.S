/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

.section .init.plat_boot_flow_hook, "ax"
.global plat_boot_flow_hook
plat_boot_flow_hook:
	/* Save the FDT pointer to a global variable. */
	adrp x25, plat_boot_flow_fdt_addr
	add x25, x25, :lo12:plat_boot_flow_fdt_addr
	str x0, [x25]
	ret
