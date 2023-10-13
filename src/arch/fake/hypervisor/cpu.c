/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/cpu.h"

#include "pg/cpu.h"
#include "pg/ffa.h"

void arch_irq_disable(void)
{
	/* TODO */
}

void arch_irq_enable(void)
{
	/* TODO */
}

void plat_interrupts_set_priority_mask(uint8_t min_priority)
{
	(void)min_priority;
}

void arch_regs_reset(struct vcpu *vcpu)
{
	/* TODO */
	(void)vcpu;
}

void arch_regs_set_pc_arg(struct arch_regs *r, ipaddr_t pc, uintreg_t arg)
{
	(void)pc;
	r->arg[0] = arg;
}

void arch_regs_set_retval(struct arch_regs *r, struct ffa_value v)
{
	r->arg[0] = v.func;
	r->arg[1] = v.arg1;
	r->arg[2] = v.arg2;
	r->arg[3] = v.arg3;
	r->arg[4] = v.arg4;
	r->arg[5] = v.arg5;
	r->arg[6] = v.arg6;
	r->arg[7] = v.arg7;
}

void arch_cpu_init(struct cpu *c, ipaddr_t entry_point)
{
	(void)c;
	(void)entry_point;
}
