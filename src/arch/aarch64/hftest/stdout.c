/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/stdout.h"

#include "vmapi/pg/call.h"

void stdout_putchar(char c)
{
	pg_debug_log(c);
}
