/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "common.h"

#include "vmapi/pg/call.h"

#include "test/hftest.h"

/**
 * Try to receive a message from the mailbox, blocking if necessary, and
 * retrying if interrupted.
 */
struct ffa_value mailbox_receive_retry(void)
{
	struct ffa_value received;

	do {
		received = ffa_msg_wait();
	} while (received.func == FFA_ERROR_32 &&
		 received.arg2 == FFA_INTERRUPTED);

	return received;
}
