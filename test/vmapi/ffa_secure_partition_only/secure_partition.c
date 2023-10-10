/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Confirms that SP has expected ID.
 */
TEST(pg_vm_get_id, secure_partition_id)
{
	EXPECT_EQ(pg_vm_get_id(), PG_VM_ID_BASE + 1);
}
