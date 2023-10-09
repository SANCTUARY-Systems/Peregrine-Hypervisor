/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

/* Define the standard types for the platform. */
#if defined(__linux__) && defined(__KERNEL__)

// #include <linux/types.h>

#define INT32_C(c) c

typedef phys_addr_t pg_ipaddr_t;

#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pg/arch/vmid_base.h"

#include "pg/vm_ids.h"

typedef uintptr_t pg_ipaddr_t;

#endif

/** Sleep value for an indefinite period of time. */
#define PG_SLEEP_INDEFINITE 0xffffffffffffffff

/** The amount of data that can be sent to a mailbox. */
#define PG_MAILBOX_SIZE 4096

/** The number of virtual interrupt IDs which are supported. */
#define PG_NUM_INTIDS 64

/** Interrupt ID returned when there is no interrupt pending. */
#define PG_INVALID_INTID 0xffffffff

/** Interrupt ID indicating the mailbox is readable. */
#define PG_MAILBOX_READABLE_INTID 1

/** Interrupt ID indicating a mailbox is writable. */
#define PG_MAILBOX_WRITABLE_INTID 2

/** The virtual interrupt ID used for the virtual timer. */
#define PG_VIRTUAL_TIMER_INTID 3

/** The virtual interrupt ID used for managed exit. */
#define PG_MANAGED_EXIT_INTID 4

/** Type of interrupts */
enum interrupt_type {
	INTERRUPT_TYPE_IRQ,
	INTERRUPT_TYPE_FIQ,
};
