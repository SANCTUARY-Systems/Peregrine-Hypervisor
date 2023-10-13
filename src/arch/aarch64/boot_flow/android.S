/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

.section .init.plat_boot_flow_hook, "ax"
.global plat_boot_flow_hook
plat_boot_flow_hook:
	/* Do nothing. */
	ret
