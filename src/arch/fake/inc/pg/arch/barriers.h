/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdatomic.h>

/** Platform-agnostic API */

/**
 * Ensures all explicit memory accesses before this point are completed before
 * any later memory accesses are performed.
 */
#define memory_ordering_barrier() atomic_thread_fence(memory_order_seq_cst)

/**
 * Ensures all explicit memory access and management instructions have completed
 * before continuing.
 *
 * FIXME: this is just a memory barrier but, without MMIO or registers to modify
 * operation in the fake architecture, this is likely enough. If there's a way
 * to have a true synchronization then we should update it.
 */
#define data_sync_barrier() atomic_thread_fence(memory_order_seq_cst)
