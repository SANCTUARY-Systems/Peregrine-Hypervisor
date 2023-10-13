/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "primary_with_secondary.h"
#include "test/hftest.h"
#include "test/vmapi/exception_handler.h"
#include "test/vmapi/ffa.h"

TEAR_DOWN(unmapped)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

/**
 * Accessing unmapped memory traps the VM.
 */
TEST(unmapped, data_unmapped)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "data_unmapped", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(exception_handler_receive_exception_count(&run_res, mb.recv),
		  1);
}

/**
 * Accessing partially unmapped memory traps the VM.
 */
TEST(unmapped, straddling_data_unmapped)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "straddling_data_unmapped", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(exception_handler_receive_exception_count(&run_res, mb.recv),
		  1);
}

/**
 * Executing unmapped memory traps the VM.
 */
TEST(unmapped, instruction_unmapped)
{
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "instruction_unmapped", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(exception_handler_receive_exception_count(&run_res, mb.recv),
		  1);
}
