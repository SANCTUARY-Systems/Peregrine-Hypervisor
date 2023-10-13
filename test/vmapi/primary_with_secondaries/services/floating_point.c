/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/std.h"
#include "pg/arch/vm/registers.h"

#include "pg/ffa.h"

#include "vmapi/pg/call.h"

#include "../msr.h"
#include "test/hftest.h"

TEST_SERVICE(fp_fill)
{
	const double value = 0.75;
	fill_fp_registers(value);
	EXPECT_EQ(ffa_yield().func, FFA_SUCCESS_32);

	ASSERT_TRUE(check_fp_register(value));
	ffa_yield();
}

TEST_SERVICE(fp_fpcr)
{
	uintreg_t value = 3 << 22; /* Set RMode to RZ */
	write_msr(fpcr, value);
	EXPECT_EQ(ffa_yield().func, FFA_SUCCESS_32);

	ASSERT_EQ(read_msr(fpcr), value);
	ffa_yield();
}
