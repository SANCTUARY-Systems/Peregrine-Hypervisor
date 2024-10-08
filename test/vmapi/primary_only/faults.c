/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include <stdalign.h>

#include "pg/arch/vm/power_mgmt.h"

#include "pg/mm.h"
#include "pg/spinlock.h"

#include "vmapi/pg/call.h"

#include "test/hftest.h"
#include "test/vmapi/ffa.h"

alignas(PAGE_SIZE) static char tx[PAGE_SIZE];
alignas(PAGE_SIZE) static char rx[PAGE_SIZE];

struct state {
	volatile bool done;
	struct spinlock lock;
};

/**
 * Releases the lock passed in, then spins reading the rx buffer.
 */
static void rx_reader(uintptr_t arg)
{
	struct state *s = (struct state *)arg;
	sl_unlock(&s->lock);

	while (!s->done) {
		*(volatile char *)(&rx[0]);
	}

	sl_unlock(&s->lock);
}

TEAR_DOWN(faults)
{
	EXPECT_FFA_ERROR(ffa_rx_release(), FFA_DENIED);
}

/**
 * Forces a spurious fault and check that Hafnium recovers from it.
 */
TEST(faults, spurious_due_to_configure)
{
	struct state s;
	alignas(4096) static uint8_t other_stack[4096];

	sl_init(&s.lock);
	s.done = false;

	/* Start secondary CPU while holding lock. */
	sl_lock(&s.lock);
	EXPECT_EQ(
		hftest_cpu_start(hftest_get_cpu_id(1), other_stack,
				 sizeof(other_stack), rx_reader, (uintptr_t)&s),
		true);

	/* Wait for CPU to release the lock. */
	sl_lock(&s.lock);

	/* Configure the VM's buffers. */
	EXPECT_EQ(ffa_rxtx_map((pg_ipaddr_t)&tx[0], (pg_ipaddr_t)&rx[0]).func,
		  FFA_SUCCESS_32);

	/* Tell other CPU to stop and wait for it. */
	s.done = true;
	sl_lock(&s.lock);
}
