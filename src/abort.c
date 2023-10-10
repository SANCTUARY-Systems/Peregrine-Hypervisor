/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/abort.h"

/**
 * Causes execution to halt and prevent progress of the current and less
 * privileged software components. This should be triggered when a
 * non-recoverable event is identified which leaves the system in an
 * inconsistent state.
 *
 * TODO: Should this also reset the system?
 */
noreturn void abort(void)
{
	/* TODO: Block all CPUs. */
	for (;;) {
		/* Prevent loop being optimized away. */
		__asm__ volatile("nop");
	}
}
