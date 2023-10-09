/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/dlog.h"
#include "pg/ffa.h"

#include "vmapi/pg/call.h"

#include "test/hftest.h"

#define RECEIVER_ID 0x8001

/**
 * Communicates with partition via direct messaging to validate functioning of
 * Direct Message interfaces.
 */
TEST(ffa_partition_to_partition_comm, dir_msg_req)
{
	const uint32_t msg[] = {0x00001111, 0x22223333, 0x44445555, 0x66667777,
				0x88889999};
	struct ffa_value res;
	ffa_vm_id_t own_id = pg_vm_get_id();

	dlog_verbose("PG_VM_ID_BASE: %x\n", PG_VM_ID_BASE);

	res = ffa_msg_send_direct_req(own_id, RECEIVER_ID, msg[0], msg[1],
				      msg[2], msg[3], msg[4]);

	EXPECT_EQ(res.func, FFA_MSG_SEND_DIRECT_RESP_32);

	EXPECT_EQ(res.arg3, msg[0]);
	EXPECT_EQ(res.arg4, msg[1]);
	EXPECT_EQ(res.arg5, msg[2]);
	EXPECT_EQ(res.arg6, msg[3]);
	EXPECT_EQ(res.arg7, msg[4]);
}
