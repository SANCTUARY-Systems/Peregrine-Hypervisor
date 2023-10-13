/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/cache.h"

void arch_cache_data_clean_range(vaddr_t start, size_t size)
{
	(void)start;
	(void)size;
}

void arch_cache_data_invalidate_range(vaddr_t start, size_t size)
{
	(void)start;
	(void)size;
}
