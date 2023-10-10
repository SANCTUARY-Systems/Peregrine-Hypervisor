/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "../msr.h"
#include "test/hftest.h"

/**
 * Test that encoding a system register using the implementation defined syntax
 * maps to the same register defined by name.
 */
TEST(arch_features, read_write_msr_impdef)
{
	uintreg_t value = 0xa;
	write_msr(S3_3_C9_C13_0, value);
	EXPECT_EQ(read_msr(S3_3_C9_C13_0), value);
	EXPECT_EQ(read_msr(PMCCNTR_EL0), value);
}
