/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "vmapi/pg/call.h"

#include "../msr.h"
#include "test/hftest.h"

#define TRY_READ(REG) dlog(#REG "=%#x\n", read_msr(REG))

#define CHECK_READ(REG, VALUE)       \
	do {                         \
		uintreg_t x;         \
		x = read_msr(REG);   \
		EXPECT_EQ(x, VALUE); \
	} while (0)

/*
 * Checks that the register can be updated. The first value is written and read
 * back and then the second value is written and read back. The values must be
 * different so that success means the register value has been changed and
 * updated as expected without relying on the initial value of the register.
 */
#define CHECK_UPDATE(REG, FROM, TO)   \
	do {                          \
		uintreg_t x;          \
		EXPECT_NE(FROM, TO);  \
		write_msr(REG, FROM); \
		x = read_msr(REG);    \
		EXPECT_EQ(x, FROM);   \
		write_msr(REG, TO);   \
		x = read_msr(REG);    \
		EXPECT_EQ(x, TO);     \
	} while (0)
