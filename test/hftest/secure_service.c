/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

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
