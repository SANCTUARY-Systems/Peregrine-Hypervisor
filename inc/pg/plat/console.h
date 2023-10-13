/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "pg/mm.h"
#include "pg/mpool.h"

/** Initialises the console hardware. */
void plat_console_init(void);

/** Initialises any memory mappings that the console driver needs. */
void plat_console_mm_init(struct mm_stage1_locked stage1_locked,
			  struct mpool *ppool);

/** Puts a single character on the console. This is a blocking call. */
void plat_console_putchar(char c);

/** Gets a single character from the console. This is a blocking call. */
char plat_console_getchar(void);
