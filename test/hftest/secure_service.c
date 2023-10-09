/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <stdalign.h>
#include <stdint.h>

#include "pg/ffa.h"
#include "pg/mm.h"
#include "pg/std.h"

#include "vmapi/pg/call.h"

#include "test/abort.h"
#include "test/hftest.h"

alignas(4096) uint8_t kstack[4096];

void test_main_sp(void);

noreturn void kmain(void)
{
	/*
	 * Initialize the stage-1 MMU and identity-map the entire address space.
	 */
	if (!hftest_mm_init()) {
		HFTEST_LOG_FAILURE();
		HFTEST_LOG(HFTEST_LOG_INDENT "Memory initialization failed");
		abort();
	}

	test_main_sp();

	/* Do not expect to get to this point, so abort. */
	abort();
}
