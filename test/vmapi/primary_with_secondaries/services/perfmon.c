/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/vm/interrupts.h"

#include "pg/dlog.h"

#include "../sysregs.h"
#include "test/vmapi/exception_handler.h"

TEST_SERVICE(perfmon_secondary_basic)
{
	exception_setup(NULL, exception_handler_skip_instruction);

	EXPECT_GT(pg_vm_get_id(), PG_PRIMARY_VM_ID);
	TRY_READ(PMCCFILTR_EL0);
	TRY_READ(PMCR_EL0);
	write_msr(PMINTENSET_EL1, 0xf);

	EXPECT_EQ(exception_handler_get_num(), 3);
	ffa_yield();
}
