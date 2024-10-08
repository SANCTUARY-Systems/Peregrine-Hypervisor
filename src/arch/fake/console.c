/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

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
