/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/dlog.h"

#include "vmapi/pg/call.h"

#include "primary_with_secondary.h"
#include "test/hftest.h"
#include "test/vmapi/exception_handler.h"
#include "test/vmapi/ffa.h"

TEAR_DOWN(boot)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

/**
 * The VM gets its memory size on boot, and can access it all.
 */
TEST(boot, memory_size)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "boot_memory", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_YIELD_32);
}

/**
 * Accessing memory outside the given range traps the VM and yields.
 */
TEST(boot, beyond_memory_size)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "boot_memory_overrun", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(exception_handler_receive_exception_count(&run_res, mb.recv),
		  1);
}

/**
 * Accessing memory before the start of the image traps the VM and yields.
 */
TEST(boot, memory_before_image)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "boot_memory_underrun", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(exception_handler_receive_exception_count(&run_res, mb.recv),
		  1);
}
