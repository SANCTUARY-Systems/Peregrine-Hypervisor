/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

.section .init.image_entry, "ax"
.weak plat_entry
plat_entry:
	/* Do nothing. */
	ret
