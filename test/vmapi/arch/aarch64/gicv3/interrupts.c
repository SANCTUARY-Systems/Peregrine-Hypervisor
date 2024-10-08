/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/irq.h"
#include "pg/arch/vm/interrupts_gicv3.h"

#include "pg/dlog.h"
#include "pg/std.h"

#include "vmapi/pg/call.h"

#include "../msr.h"
#include "gicv3.h"
#include "test/hftest.h"
#include "test/vmapi/ffa.h"

SET_UP(interrupts)
{
	system_setup();
}

SET_UP(interrupts_ffa_direct_msg)
{
	system_setup();

	EXPECT_EQ(ffa_rxtx_map(send_page_addr, recv_page_addr).func,
		  FFA_SUCCESS_32);
	SERVICE_SELECT(SERVICE_VM1, "interrupts_secondary_direct_message",
		       send_buffer);

	interrupt_set_priority_mask(0xff);
	interrupt_set_priority(1, 0x80);

	/* Enable interrupts at GIC level but keep CPU PSTATE.I masked. */
	interrupt_enable_all(true);
}

TEAR_DOWN(interrupts_ffa_direct_msg)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

TEST(interrupts, enable_sgi)
{
	/* Interrupt IDs 0 to 15 are SGIs. */
	uint8_t intid = 3;
	interrupt_set_priority_mask(0xff);
	interrupt_set_priority(intid, 0x80);
	arch_irq_enable();
	interrupt_enable_all(true);
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	/* Send ourselves the SGI. */
	last_interrupt_id = 0xffffffff;
	dlog("sending SGI\n");
	interrupt_send_sgi(intid, false, 0, 0, 0, 1);
	dlog("sent SGI\n");

	/* Check that we got it, and we are back to not active or pending. */
	EXPECT_EQ(last_interrupt_id, intid);
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
}

TEST(interrupts, disable_sgi)
{
	/* Interrupt IDs 0 to 15 are SGIs. */
	uint8_t intid = 3;
	interrupt_enable_all(true);
	interrupt_enable(intid, false);
	interrupt_set_priority_mask(0xff);
	interrupt_set_priority(intid, 0x80);
	arch_irq_enable();
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	/* Send ourselves the SGI. */
	last_interrupt_id = 0xffffffff;
	dlog("sending SGI\n");
	interrupt_send_sgi(intid, false, 0, 0, 0, 1);
	dlog("sent SGI\n");

	/* Check that we didn't get it, but it is pending (and not active). */
	EXPECT_EQ(last_interrupt_id, 0xffffffff);
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0x1 << intid);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
}

TEST(interrupts, physical_timer)
{
	interrupt_enable(PHYSICAL_TIMER_IRQ, true);
	interrupt_set_priority(PHYSICAL_TIMER_IRQ, 0x80);
	interrupt_set_edge_triggered(PHYSICAL_TIMER_IRQ, true);
	interrupt_set_priority_mask(0xff);
	arch_irq_enable();

	/*
	 * Check that no (SGI or PPI) interrupts are active or pending to start
	 * with.
	 */
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	dlog("Starting timer\n");
	/* Set physical timer for 1 tick. */
	write_msr(CNTP_TVAL_EL0, 1);
	/* Enable it. */
	write_msr(CNTP_CTL_EL0, 0x00000001);

	dlog("waiting for interrupt\n");
	while (last_interrupt_id == 0) {
		EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
		EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
		EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
		EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
	}

	/* Check that we got the interrupt. */
	dlog("Checking for interrupt\n");
	EXPECT_EQ(last_interrupt_id, PHYSICAL_TIMER_IRQ);
	/* Check timer status. */
	EXPECT_EQ(read_msr(CNTP_CTL_EL0), 0x00000005);

	/* There should again be no pending or active interrupts. */
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
}

TEST(interrupts, virtual_timer)
{
	interrupt_enable(VIRTUAL_TIMER_IRQ, true);
	interrupt_set_priority(VIRTUAL_TIMER_IRQ, 0x80);
	interrupt_set_edge_triggered(VIRTUAL_TIMER_IRQ, true);
	interrupt_set_priority_mask(0xff);
	arch_irq_enable();

	/* Check that no interrupts are active or pending to start with. */
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	dlog("Starting timer\n");
	/* Set virtual timer for 1 tick. */
	write_msr(CNTV_TVAL_EL0, 1);
	/* Enable it. */
	write_msr(CNTV_CTL_EL0, 0x00000001);

	dlog("Waiting for interrupt\n");
	while (last_interrupt_id == 0) {
		EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
		EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
		EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
		EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
	}

	/* Check that we got the interrupt. */
	dlog("Checking for interrupt\n");
	EXPECT_EQ(last_interrupt_id, VIRTUAL_TIMER_IRQ);
	/* Check timer status. */
	EXPECT_EQ(read_msr(CNTV_CTL_EL0), 0x00000005);

	/* There should again be no pending or active interrupts. */
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);
}

/**
 * The secondary vCPU is interrupted by a physical interrupt while processing
 * a direct message request (causing Hafnium to return to the primary VM), then
 * the secondary VM is resumed by ffa_run and returns a direct message response.
 */
TEST(interrupts_ffa_direct_msg, secondary_interrupted_by_sgi)
{
	struct ffa_value res;

	/* Let the secondary get started and wait for a message. */
	res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(res.func, FFA_MSG_WAIT_32);
	EXPECT_EQ(res.arg2, FFA_SLEEP_INDEFINITE);

	/* Check that no interrupts are active or pending to start with. */
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISPENDR0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	/* Send ourselves the SGI. */
	last_interrupt_id = 0xffffffff;
	dlog("sending SGI\n");
	interrupt_send_sgi(1, false, 0, 0, 0, 1);

	/* Send a direct message request. */
	res = ffa_msg_send_direct_req(PG_PRIMARY_VM_ID, SERVICE_VM1, 1, 0, 0, 0,
				      0);

	/* The secondary VM has been pre-empted by the PVM. */
	EXPECT_EQ(res.func, FFA_INTERRUPT_32);

	/* Unmasking local interrupts triggers the pending SGI. */
	arch_irq_enable();

	/* Check that we got it, and we are back to not active or pending. */
	EXPECT_EQ(last_interrupt_id, 1);
	EXPECT_EQ(io_read32_array(GICD_ISPENDR, 0), 0);
	EXPECT_EQ(io_read32_array(GICD_ISACTIVER, 0), 0);
	EXPECT_EQ(io_read32(GICR_ISACTIVER0), 0);

	/*
	 * Resume the secondary VM for it to send the direct message
	 * response.
	 */
	res = ffa_run(SERVICE_VM1, 0);
	EXPECT_EQ(res.func, FFA_MSG_SEND_DIRECT_RESP_32);
	EXPECT_EQ(res.arg3, 2);
}
