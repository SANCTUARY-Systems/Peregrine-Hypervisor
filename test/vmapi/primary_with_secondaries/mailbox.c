/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <stdint.h>

#include "pg/ffa.h"
#include "pg/std.h"

#include "vmapi/pg/call.h"

#include "primary_with_secondary.h"
#include "test/hftest.h"
#include "test/vmapi/ffa.h"

/**
 * Reverses the order of the elements in the given array.
 */
static void reverse(char *s, size_t len)
{
	size_t i;

	for (i = 0; i < len / 2; i++) {
		char t = s[i];
		s[i] = s[len - 1 - i];
		s[len - 1 - i] = t;
	}
}

/**
 * Finds the next lexicographic permutation of the given array, if there is one.
 */
static void next_permutation(char *s, size_t len)
{
	size_t i;
	size_t j;

	for (i = len - 2; i < len; i--) {
		const char t = s[i];
		if (t >= s[i + 1]) {
			continue;
		}

		for (j = len - 1; t >= s[j]; j--) {
		}

		s[i] = s[j];
		s[j] = t;
		reverse(s + i + 1, len - i - 1);
		return;
	}
}

TEAR_DOWN(mailbox)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

/**
 * Clearing an empty mailbox is an error.
 */
TEST(mailbox, clear_empty)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

/**
 * Send and receive the same message from the echo VM.
 */
TEST(mailbox, echo)
{
	const char message[] = "Echo this back to me!";
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "echo", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/* Set the message, echo it and check it didn't change. */
	memcpy_s(mb.send, FFA_MSG_PAYLOAD_MAX, message, sizeof(message));
	EXPECT_EQ(
		ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, sizeof(message), 0)
			.func,
		FFA_SUCCESS_32);
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
	EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
	EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);
	EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
}

/**
 * Repeatedly send a message and receive it back from the echo VM.
 */
TEST(mailbox, repeated_echo)
{
	char message[] = "Echo this back to me!";
	struct ffa_value run_res;
	uint8_t i;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "echo", mb.send);

	for (i = 0; i < 100; i++) {
		/* Run secondary until it reaches the wait for messages. */
		run_res = ffa_run(SERVICE_VM1, 0);
		EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
		EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

		/* Set the message, echo it and check it didn't change. */
		next_permutation(message, sizeof(message) - 1);
		memcpy_s(mb.send, FFA_MSG_PAYLOAD_MAX, message,
			 sizeof(message));
		EXPECT_EQ(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1,
				       sizeof(message), 0)
				  .func,
			  FFA_SUCCESS_32);
		run_res = ffa_run(SERVICE_VM1, 0);
		EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
		EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
		EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);
		EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
	}
}

/**
 * Send a message to relay_a which will forward it to relay_b where it will be
 * sent back here.
 */
TEST(mailbox, relay)
{
	const char message[] = "Send this round the relay!";
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "relay", mb.send);
	SERVICE_SELECT(SERVICE_VM2, "relay", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);
	run_res = ffa_run(SERVICE_VM2, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/*
	 * Build the message chain so the message is sent from here to
	 * SERVICE_VM1, then to SERVICE_VM2 and finally back to here.
	 */
	{
		ffa_vm_id_t *chain = (ffa_vm_id_t *)mb.send;
		*chain++ = htole32(SERVICE_VM2);
		*chain++ = htole32(PG_PRIMARY_VM_ID);
		memcpy_s(chain, FFA_MSG_PAYLOAD_MAX - (2 * sizeof(ffa_vm_id_t)),
			 message, sizeof(message));

		EXPECT_EQ(
			ffa_msg_send(
				PG_PRIMARY_VM_ID, SERVICE_VM1,
				sizeof(message) + (2 * sizeof(ffa_vm_id_t)), 0)
				.func,
			FFA_SUCCESS_32);
	}

	/* Let SERVICE_VM1 forward the message. */
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
	EXPECT_EQ(ffa_receiver(run_res), SERVICE_VM2);
	EXPECT_EQ(ffa_msg_send_size(run_res), 0);

	/* Let SERVICE_VM2 forward the message. */
	run_res = ffa_run(SERVICE_VM2, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);

	/* Ensure the message is intact. */
	EXPECT_EQ(ffa_receiver(run_res), PG_PRIMARY_VM_ID);
	EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
	EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);
	EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
}

/**
 * Send a message before the secondary VM is configured, but do not register
 * for notification. Ensure we're not notified.
 */
TEST(mailbox, no_primary_to_secondary_notification_on_configure)
{
	struct ffa_value run_res;

	set_up_mailbox();

	EXPECT_FFA_ERROR(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, 0, 0),
			 FFA_BUSY);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	EXPECT_EQ(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, 0, 0).func,
		  FFA_SUCCESS_32);
}

/**
 * Send a message before the secondary VM is configured, and receive a
 * notification when it configures.
 */
TEST(mailbox, secondary_to_primary_notification_on_configure)
{
	struct ffa_value run_res;

	set_up_mailbox();

	EXPECT_FFA_ERROR(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, 0,
				      FFA_MSG_SEND_NOTIFY),
			 FFA_BUSY);

	/*
	 * Run first VM for it to configure itself. It should result in
	 * notifications having to be issued.
	 */
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_RX_RELEASE_32);

	/* A single waiter is returned. */
	EXPECT_EQ(pg_mailbox_waiter_get(SERVICE_VM1), PG_PRIMARY_VM_ID);
	EXPECT_EQ(pg_mailbox_waiter_get(SERVICE_VM1), -1);

	/* Send should now succeed. */
	EXPECT_EQ(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, 0, 0).func,
		  FFA_SUCCESS_32);
}

/**
 * Causes secondary VM to send two messages to primary VM. The second message
 * will reach the mailbox while it's not writable. Checks that notifications are
 * properly delivered when mailbox is cleared.
 */
TEST(mailbox, primary_to_secondary)
{
	char message[] = "not ready echo";
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "echo_with_notification", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/* Send a message to echo service, and get response back. */
	memcpy_s(mb.send, FFA_MSG_PAYLOAD_MAX, message, sizeof(message));
	EXPECT_EQ(
		ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, sizeof(message), 0)
			.func,
		FFA_SUCCESS_32);
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
	EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
	EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);

	/* Let secondary VM continue running so that it will wait again. */
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/* Without clearing our mailbox, send message again. */
	reverse(message, strnlen_s(message, sizeof(message)));
	memcpy_s(mb.send, FFA_MSG_PAYLOAD_MAX, message, sizeof(message));

	/* Message should be dropped since the mailbox was not cleared. */
	EXPECT_EQ(
		ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, sizeof(message), 0)
			.func,
		FFA_SUCCESS_32);
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, PG_FFA_RUN_WAIT_FOR_INTERRUPT);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/* Clear the mailbox. We expect to be told there are pending waiters. */
	EXPECT_EQ(ffa_rx_release().func, FFA_RX_RELEASE_32);

	/* Retrieve a single waiter. */
	EXPECT_EQ(pg_mailbox_waiter_get(PG_PRIMARY_VM_ID), SERVICE_VM1);
	EXPECT_EQ(pg_mailbox_waiter_get(PG_PRIMARY_VM_ID), -1);

	/*
	 * Inject interrupt into VM and let it run again. We should receive
	 * the echoed message.
	 */
	EXPECT_EQ(
		pg_interrupt_inject(SERVICE_VM1, 0, PG_MAILBOX_WRITABLE_INTID),
		1);
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
	EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
	EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);
	EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
}

/**
 * Sends two messages to secondary VM without letting it run, so second message
 * won't go through. Ensure that a notification is delivered when secondary VM
 * clears the mailbox.
 */
TEST(mailbox, secondary_to_primary_notification)
{
	const char message[] = "not ready echo";
	struct ffa_value run_res;
	struct mailbox_buffers mb = set_up_mailbox();

	SERVICE_SELECT(SERVICE_VM1, "echo_with_notification", mb.send);

	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(run_res.arg2, FFA_SLEEP_INDEFINITE);

	/* Send a message to echo service twice. The second should fail. */
	memcpy_s(mb.send, FFA_MSG_PAYLOAD_MAX, message, sizeof(message));
	EXPECT_EQ(
		ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, sizeof(message), 0)
			.func,
		FFA_SUCCESS_32);
	EXPECT_FFA_ERROR(ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1,
				      sizeof(message), FFA_MSG_SEND_NOTIFY),
			 FFA_BUSY);

	/* Receive a reply for the first message. */
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_MSG_SEND_32);
	EXPECT_EQ(ffa_msg_send_size(run_res), sizeof(message));
	EXPECT_EQ(memcmp(mb.recv, message, sizeof(message)), 0);
	EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);

	/* Run VM again so that it clears its mailbox. */
	run_res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(run_res.func, FFA_RX_RELEASE_32);

	/* Retrieve a single waiter. */
	EXPECT_EQ(pg_mailbox_waiter_get(SERVICE_VM1), PG_PRIMARY_VM_ID);
	EXPECT_EQ(pg_mailbox_waiter_get(SERVICE_VM1), -1);

	/* Send should now succeed. */
	EXPECT_EQ(
		ffa_msg_send(PG_PRIMARY_VM_ID, SERVICE_VM1, sizeof(message), 0)
			.func,
		FFA_SUCCESS_32);
}
