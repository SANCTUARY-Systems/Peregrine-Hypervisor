/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

/**
 * Generic implementation of a spinlock using C11 atomics.
 * Does not work very well under contention.
 */

#include <stdatomic.h>

#ifdef _STDATOMIC_HAVE_ATOMIC
using std::atomic_flag;
#endif

struct spinlock {
	atomic_flag v;
};

#define SPINLOCK_INIT ((struct spinlock){.v = ATOMIC_FLAG_INIT})

static inline void sl_init(struct spinlock *l)
{
#if !defined(__cplusplus)
	*l = SPINLOCK_INIT;
#endif
}

static inline void sl_lock(struct spinlock *l)
{
	while (atomic_flag_test_and_set_explicit(&l->v, memory_order_acquire)) {
		/* do nothing */
	}
}

static inline void sl_unlock(struct spinlock *l)
{
	atomic_flag_clear_explicit(&l->v, memory_order_release);
}
