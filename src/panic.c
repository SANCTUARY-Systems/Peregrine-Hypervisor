/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/panic.h"

#include <stdarg.h>

#include "pg/abort.h"
#include "pg/dlog.h"

/**
 * Logs a reason before calling abort.
 *
 * TODO: Determine if we want to omit strings on non-debug builds.
 */
noreturn void panic(const char *fmt, ...)
{
	va_list args;

	dlog("Panic: ");

	va_start(args, fmt);
	vdlog(fmt, args);
	va_end(args);

	dlog("\n");

	abort();
}
