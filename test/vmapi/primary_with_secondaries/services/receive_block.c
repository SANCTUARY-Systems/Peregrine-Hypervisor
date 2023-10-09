/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/irq.h"
#include "pg/arch/vm/interrupts.h"

#include "pg/dlog.h"
#include "pg/ffa.h"

#include "vmapi/pg/call.h"

#include "primary_with_secondary.h"
#include "test/hftest.h"
#include "test/vmapi/ffa.h"

/*
 * Secondary VM that enables an interrupt, disables interrupts globally, and
 * calls pg_mailbox_receive with block=true but expects it to fail.
 */

static void irq(void)
{
	uint32_t interrupt_id = pg_interrupt_get();
	FAIL("Unexpected secondary IRQ %d from current", interrupt_id);
}

TEST_SERVICE(receive_block)
{
	int32_t i;
	const char message[] = "Done waiting";

	exception_setup(irq, NULL);
	arch_irq_disable();
	pg_interrupt_enable(EXTERNAL_INTERRUPT_ID_A, true, INTERRUPT_TYPE_IRQ);

	for (i = 0; i < 10; ++i) {
		struct ffa_value res = ffa_msg_wait();
		EXPECT_FFA_ERROR(res, FFA_INTERRUPTED);
	}

	memcpy_s(SERVICE_SEND_BUFFER(), FFA_MSG_PAYLOAD_MAX, message,
		 sizeof(message));

	ffa_msg_send(pg_vm_get_id(), PG_PRIMARY_VM_ID, sizeof(message), 0);
}
