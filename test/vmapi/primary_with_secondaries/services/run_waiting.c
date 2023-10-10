/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Service that waits for a message but expects never to get one.
 */
TEST_SERVICE(run_waiting)
{
	ffa_msg_wait();

	FAIL("Secondary VM was run.");
}
