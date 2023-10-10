/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "sysregs.h"

#include "pg/arch/vm/interrupts.h"

#include "primary_with_secondary.h"
#include "test/vmapi/exception_handler.h"
#include "test/vmapi/ffa.h"

SET_UP(sysregs)
{
	exception_setup(NULL, exception_handler_skip_instruction);
}

/**
 * Test that accessing LOR registers would inject an exception.
 */
TEST(sysregs, lor_exception)
{
	EXPECT_EQ(pg_vm_get_id(), PG_PRIMARY_VM_ID);
	TRY_READ(MSR_LORC_EL1);

	EXPECT_EQ(exception_handler_get_num(), 1);
}
