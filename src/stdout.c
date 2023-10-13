/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/stdout.h"

#include "pg/plat/console.h"

void stdout_putchar(char c)
{
	plat_console_putchar(c);
}
