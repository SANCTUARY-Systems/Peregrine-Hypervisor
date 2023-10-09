/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/addr.h"
#include "pg/mm.h"
#include "pg/types.h"

#define PPI_IRQ_BASE 16
#define PHYSICAL_TIMER_IRQ (PPI_IRQ_BASE + 14)
#define VIRTUAL_TIMER_IRQ (PPI_IRQ_BASE + 11)
#define HYPERVISOR_TIMER_IRQ (PPI_IRQ_BASE + 10)

#define NANOS_PER_UNIT 1000000000

#define SERVICE_VM1 (PG_VM_ID_OFFSET + 1)

extern alignas(PAGE_SIZE) uint8_t send_page[PAGE_SIZE];
extern alignas(PAGE_SIZE) uint8_t recv_page[PAGE_SIZE];

extern pg_ipaddr_t send_page_addr;
extern pg_ipaddr_t recv_page_addr;

extern void *send_buffer;
extern void *recv_buffer;

extern volatile uint32_t last_interrupt_id;

void system_setup();
