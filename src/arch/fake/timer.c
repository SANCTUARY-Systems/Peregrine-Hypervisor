/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/timer.h"

#include <stdbool.h>
#include <stdint.h>

#include "pg/arch/types.h"

bool arch_timer_pending(struct arch_regs *regs)
{
	/* TODO */
	(void)regs;
	return false;
}

void arch_timer_mask(struct arch_regs *regs)
{
	/* TODO */
	(void)regs;
}

bool arch_timer_enabled(struct arch_regs *regs)
{
	/* TODO */
	(void)regs;
	return false;
}

uint64_t arch_timer_remaining_ns(struct arch_regs *regs)
{
	/* TODO */
	(void)regs;
	return 0;
}

bool arch_timer_enabled_current(void)
{
	/* TODO */
	return false;
}

void arch_timer_disable_current(void)
{
	/* TODO */
}

uint64_t arch_timer_remaining_ns_current(void)
{
	/* TODO */
	return 0;
}
