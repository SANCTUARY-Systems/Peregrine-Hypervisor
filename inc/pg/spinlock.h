/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

/*
 * Includes the arch-specific definition of 'struct spinlock' and
 * implementations of:
 *  - SPINLOCK_INIT
 *  - sl_lock()
 *  - sl_unlock()
 */
#include "pg/arch/spinlock.h"

/**
 * Locks both locks, enforcing the lowest address first ordering for locks of
 * the same kind.
 */
static inline void sl_lock_both(struct spinlock *a, struct spinlock *b)
{
	if (a < b) {
		sl_lock(a);
		sl_lock(b);
	} else {
		sl_lock(b);
		sl_lock(a);
	}
}
