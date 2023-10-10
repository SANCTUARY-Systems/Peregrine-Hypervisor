/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include <stdalign.h>
#include <stdint.h>

#include "pg/mm.h"
#include "pg/std.h"

#include "test/hftest.h"
#include "test/hftest_impl.h"

HFTEST_ENABLE();

static struct hftest_context global_context;

struct hftest_context *hftest_get_context(void)
{
	return &global_context;
}

noreturn void abort(void)
{
	HFTEST_LOG("Service contained failures.");
	/* Cause a fault, as a secondary/SP can't power down the machine. */
	*((volatile uint8_t *)1) = 1;

	/* This should never be reached, but to make the compiler happy... */
	for (;;) {
	}
}
