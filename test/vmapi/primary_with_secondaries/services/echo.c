/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/ffa.h"
#include "pg/std.h"

#include "vmapi/pg/call.h"

#include "test/hftest.h"

TEST_SERVICE(echo)
{
	/* Loop, echo messages back to the sender. */
	for (;;) {
		struct ffa_value ret = ffa_msg_wait();
		ffa_vm_id_t target_vm_id = ffa_receiver(ret);
		ffa_vm_id_t source_vm_id = ffa_sender(ret);
		void *send_buf = SERVICE_SEND_BUFFER();
		void *recv_buf = SERVICE_RECV_BUFFER();

		ASSERT_EQ(ret.func, FFA_MSG_SEND_32);
		memcpy_s(send_buf, FFA_MSG_PAYLOAD_MAX, recv_buf,
			 ffa_msg_send_size(ret));

		EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
		ffa_msg_send(target_vm_id, source_vm_id, ffa_msg_send_size(ret),
			     0);
	}
}
