/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pg/arch/types.h"

/**
 * Sets the bit to mask virtual timer interrupts.
 */
void arch_timer_mask(struct arch_regs *regs);

/**
 * Checks whether the virtual timer is enabled and its interrupt not masked.
 */
bool arch_timer_enabled(struct arch_regs *regs);

/**
 * Returns the number of nanoseconds remaining on the virtual timer as stored in
 * the given `arch_regs`, or 0 if it has already expired. This is undefined if
 * the timer is not enabled.
 */
uint64_t arch_timer_remaining_ns(struct arch_regs *regs);

/**
 * Returns whether the timer is ready to fire: i.e. it is enabled, not masked,
 * and the condition is met.
 */
bool arch_timer_pending(struct arch_regs *regs);

/**
 * Checks whether the virtual timer is enabled and its interrupt not masked, for
 * the currently active vCPU.
 */
bool arch_timer_enabled_current(void);

/**
 * Disable the virtual timer for the currently active vCPU.
 */
void arch_timer_disable_current(void);

/**
 * Returns the number of nanoseconds remaining on the virtual timer of the
 * currently active vCPU, or 0 if it has already expired. This is undefined if
 * the timer is not enabled.
 */
uint64_t arch_timer_remaining_ns_current(void);
