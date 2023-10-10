/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/plat/console.h"

#include <stdio.h>

#include "pg/mm.h"
#include "pg/mpool.h"

void plat_console_init(void)
{
}

void plat_console_mm_init(struct mm_stage1_locked stage1_locked,
			  struct mpool *ppool)
{
}

void plat_console_putchar(char c)
{
	putchar(c);
}
