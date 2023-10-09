/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/irq.h"
#include "pg/arch/vm/interrupts.h"

#include "pg/dlog.h"
#include "pg/std.h"

#include "vmapi/pg/call.h"
#include "vmapi/pg/ffa.h"

#include "primary_with_secondary.h"
#include "test/hftest.h"

/*
 * Secondary VM that sends messages in response to interrupts, and interrupts
 * itself when it receives a message.
 */

static void irq(void)
{
	uint32_t interrupt_id = pg_interrupt_get();
	char buffer[] = "Got IRQ xx.";
	int size = sizeof(buffer);
	dlog("secondary IRQ %d from current\n", interrupt_id);
	buffer[8] = '0' + interrupt_id / 10;
	buffer[9] = '0' + interrupt_id % 10;
	memcpy_s(SERVICE_SEND_BUFFER(), FFA_MSG_PAYLOAD_MAX, buffer, size);
	ffa_msg_send(pg_vm_get_id(), PG_PRIMARY_VM_ID, size, 0);
	dlog("secondary IRQ %d ended\n", interrupt_id);
}

/**
 * Try to receive a message from the mailbox, blocking if necessary, and
 * retrying if interrupted.
 */
struct ffa_value mailbox_receive_retry()
{
	struct ffa_value received;

	do {
		received = ffa_msg_wait();
	} while (received.func == FFA_ERROR_32 &&
		 ffa_error_code(received) == FFA_INTERRUPTED);

	return received;
}

TEST_SERVICE(interruptible)
{
	ffa_vm_id_t this_vm_id = pg_vm_get_id();
	void *recv_buf = SERVICE_RECV_BUFFER();

	exception_setup(irq, NULL);
	pg_interrupt_enable(SELF_INTERRUPT_ID, true, INTERRUPT_TYPE_IRQ);
	pg_interrupt_enable(EXTERNAL_INTERRUPT_ID_A, true, INTERRUPT_TYPE_IRQ);
	pg_interrupt_enable(EXTERNAL_INTERRUPT_ID_B, true, INTERRUPT_TYPE_IRQ);
	arch_irq_enable();

	for (;;) {
		const char ping_message[] = "Ping";
		const char enable_message[] = "Enable interrupt C";

		struct ffa_value ret = mailbox_receive_retry();

		ASSERT_EQ(ret.func, FFA_MSG_SEND_32);
		if (ffa_sender(ret) == PG_PRIMARY_VM_ID &&
		    ffa_msg_send_size(ret) == sizeof(ping_message) &&
		    memcmp(recv_buf, ping_message, sizeof(ping_message)) == 0) {
			/* Interrupt ourselves */
			pg_interrupt_inject(this_vm_id, 0, SELF_INTERRUPT_ID);
		} else if (ffa_sender(ret) == PG_PRIMARY_VM_ID &&
			   ffa_msg_send_size(ret) == sizeof(enable_message) &&
			   memcmp(recv_buf, enable_message,
				  sizeof(enable_message)) == 0) {
			/* Enable interrupt ID C. */
			pg_interrupt_enable(EXTERNAL_INTERRUPT_ID_C, true,
					    INTERRUPT_TYPE_IRQ);
		} else {
			dlog("Got unexpected message from VM %d, size %d.\n",
			     ffa_sender(ret), ffa_msg_send_size(ret));
			FAIL("Unexpected message");
		}
		EXPECT_EQ(ffa_rx_release().func, FFA_SUCCESS_32);
	}
}
