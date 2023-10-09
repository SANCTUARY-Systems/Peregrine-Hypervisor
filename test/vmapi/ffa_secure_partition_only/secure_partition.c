/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Confirms that SP has expected ID.
 */
TEST(pg_vm_get_id, secure_partition_id)
{
	EXPECT_EQ(pg_vm_get_id(), PG_VM_ID_BASE + 1);
}
