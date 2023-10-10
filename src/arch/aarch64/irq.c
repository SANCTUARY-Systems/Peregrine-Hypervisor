/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/irq.h"

void arch_irq_disable(void)
{
	__asm__ volatile("msr DAIFSet, #0xf");
}

void arch_irq_enable(void)
{
	__asm__ volatile("msr DAIFClr, #0xf");
}
