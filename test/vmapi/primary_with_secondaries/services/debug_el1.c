/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/vm/interrupts.h"

#include "pg/dlog.h"

#include "../sysregs.h"
#include "test/vmapi/exception_handler.h"

TEST_SERVICE(debug_el1_secondary_basic)
{
	exception_setup(NULL, exception_handler_skip_instruction);

	EXPECT_GT(pg_vm_get_id(), PG_PRIMARY_VM_ID);
	TRY_READ(MDCCINT_EL1);
	TRY_READ(DBGBCR0_EL1);
	TRY_READ(DBGBVR0_EL1);
	TRY_READ(DBGWCR0_EL1);
	TRY_READ(DBGWVR0_EL1);

	EXPECT_EQ(exception_handler_get_num(), 5);
	ffa_yield();
}
