/*
 * Copyright 2020 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Confirms the primary VM has the primary ID.
 */
TEST(pg_vm_get_id, primary_has_primary_id)
{
	EXPECT_EQ(pg_vm_get_id(), PG_PRIMARY_VM_ID);
}
