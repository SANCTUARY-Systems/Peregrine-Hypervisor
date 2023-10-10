/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

/** AArch64-specific mapping modes */

/** Mapping mode defining MMU Stage-1 block/page non-secure bit */
#define MM_MODE_NS UINT32_C(0x0080)
