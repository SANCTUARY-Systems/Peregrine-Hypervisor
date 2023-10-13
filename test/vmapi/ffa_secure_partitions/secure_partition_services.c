/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Message loop to add tests to be controlled by the control partition(depends
 * on the test set-up).
 * TODO: Extend/refactor function below to cater for other tests, in addition to
 * the current simple 'echo' via direct message.
 */
noreturn void test_main_sp(void)
{
	struct ffa_value res;

	res = ffa_msg_wait();

	while (1) {
		HFTEST_LOG("Received direct message request");
		EXPECT_EQ(res.func, FFA_MSG_SEND_DIRECT_REQ_32);

		ffa_msg_send_direct_resp(ffa_receiver(res), ffa_sender(res),
					 res.arg3, res.arg4, res.arg5, res.arg6,
					 res.arg7);
	}
}
