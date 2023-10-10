/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include "pg/mpool.h"   /* struct mpool */

/**
 * Performs arch specific boot time initialization.
 *
 * It must only be called once, on first boot and must be called as early as
 * possible.
 */
void arch_one_time_init(void);

struct mpool *get_ppool(void);

