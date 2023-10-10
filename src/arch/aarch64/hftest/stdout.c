/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/stdout.h"

#include "vmapi/pg/call.h"

void stdout_putchar(char c)
{
	pg_debug_log(c);
}
