/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Confirms the primary VM has the primary ID.
 */
TEST(pg_vm_get_id, primary_has_primary_id)
{
	EXPECT_EQ(pg_vm_get_id(), PG_PRIMARY_VM_ID);
}
