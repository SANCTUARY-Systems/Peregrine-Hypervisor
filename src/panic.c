/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

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
