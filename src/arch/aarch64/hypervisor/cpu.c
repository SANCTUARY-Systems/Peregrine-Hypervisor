/*
 * Copyright (c) 2023 SANCTUARY Systems GmbH
 *
 * This file is free software: you may copy, redistribute and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * For a commercial license, please contact SANCTUARY Systems GmbH
 * directly at info@sanctuary.dev
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *     Copyright 2018 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/cpu.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pg/arch/plat/psci.h"

#include "pg/addr.h"
#include "pg/ffa.h"
#include "pg/plat/interrupts.h"
#include "pg/std.h"
#include "pg/vm.h"
#include "pg/arch/emulator.h"

#include "feature_id.h"
#include "msr.h"
#include "perfmon.h"
#include "sysregs.h"

#if BRANCH_PROTECTION

__uint128_t pauth_apia_key;

#endif

/**
 * The LO field indicates whether LORegions are supported.
 */
#define ID_AA64MMFR1_EL1_LO (UINT64_C(1) << 16)

static void lor_disable(void)
{
	/*
	 * Accesses to LORC_EL1 are undefined if LORegions are not supported.
	 */
	if (read_msr(ID_AA64MMFR1_EL1) & ID_AA64MMFR1_EL1_LO) {
		write_msr(MSR_LORC_EL1, 0);
	}
}

static void gic_regs_reset(struct arch_regs *r, bool is_primary)
{
#if GIC_VERSION == 3 || GIC_VERSION == 4
	uint32_t ich_hcr = 0;
	uint32_t icc_sre_el2 =
		(1U << 0) | /* SRE, enable ICH_* and ICC_* at EL2. */
		(0x3 << 1); /* DIB and DFB, disable IRQ/FIQ bypass. */

	if (is_primary) {
		icc_sre_el2 |= 1U << 3; /* Enable EL1 access to ICC_SRE_EL1. */
		ich_hcr = 1U << 10; /* TC bit */
	} else {
		/* Trap EL1 access to GICv3 system registers. */
		ich_hcr =
			(0x1fU << 10); /* TDIR, TSEI, TALL1, TALL0, TC bits. */
	}
	r->gic.ich_hcr_el2 = ich_hcr;
	r->gic.icc_sre_el2 = icc_sre_el2;
#endif
}

void arch_regs_reset(struct vcpu *vcpu)
{
	uint16_t vm_id = vcpu->vm->id;
	bool is_primary = vm_id == PG_PRIMARY_VM_ID;
	cpu_id_t vcpu_id = is_primary ? vcpu->cpu->id : vcpu_index(vcpu);

	paddr_t table = vcpu->vm->ptable.root;
	struct arch_regs *r = &vcpu->regs;
	uintreg_t pc = r->pc;
	uintreg_t arg = r->r[0];
	uintreg_t cnthctl;

	memset_s(r, sizeof(*r), 0, sizeof(*r));

	r->pc = pc;
	r->r[0] = arg;

	cnthctl = 0;

	if (is_primary) {
		/*
		 * cnthctl_el2 is redefined when VHE is enabled.
		 * EL1PCTEN, don't trap phys cnt access.
		 * EL1PCEN, don't trap phys timer access.
		 */
		if (has_vhe_support()) {
			cnthctl |= (1U << 10) | (1U << 11);
		} else {
			cnthctl |= (1U << 0) | (1U << 1);
		}
	}

//	r->hcr_el2 = get_hcr_el2_value(vm_id);
	r->hcr_el2 = get_hcr_el2_value(PG_PRIMARY_VM_ID); //TODO: distinguish between full VMs (Linux VMs) and FFA VMs
	r->lazy.cnthctl_el2 = cnthctl;
	r->lazy.vttbr_el2 = pa_addr(table) | ((uint64_t)vm_id << 48);
	r->lazy.vmpidr_el2 = vcpu_id;
	/* Mask (disable) interrupts and run in EL1h mode. */
	r->spsr = PSR_D | PSR_A | PSR_I | PSR_F | PSR_PE_MODE_EL1H;

	r->lazy.mdcr_el2 = get_mdcr_el2_value();

	/*
	 * NOTE: It is important that MDSCR_EL1.MDE (bit 15) is set to 0 for
	 * secondary VMs as long as Peregrine does not support debug register
	 * access for secondary VMs. If adding Peregrine support for secondary VM
	 * debug register accesses, then on context switches Peregrine needs to
	 * save/restore EL1 debug register state that either might change, or
	 * that needs to be protected.
	 */
	r->lazy.mdscr_el1 = 0x0U & ~(0x1U << 15);

	/* Disable cycle counting on initialization. */
	r->lazy.pmccfiltr_el0 = perfmon_get_pmccfiltr_el0_init_value(vm_id);

	/* Set feature-specific register values. */
	feature_set_traps(vcpu->vm, r);

	//gic_regs_reset(r, is_primary);
	gic_regs_reset(r, true); //TODO: distinguish between full VMs (Linux VMs) and FFA VMs
}

void arch_regs_set_pc_arg(struct arch_regs *r, ipaddr_t pc, uintreg_t arg)
{
	r->pc = ipa_addr(pc);
	r->r[0] = arg;
}

void arch_regs_set_retval(struct arch_regs *r, struct ffa_value v)
{
	r->r[0] = v.func;
	r->r[1] = v.arg1;
	r->r[2] = v.arg2;
	r->r[3] = v.arg3;
	r->r[4] = v.arg4;
	r->r[5] = v.arg5;
	r->r[6] = v.arg6;
	r->r[7] = v.arg7;
}

struct ffa_value arch_regs_get_args(struct arch_regs *regs)
{
	return (struct ffa_value){
		.func = regs->r[0],
		.arg1 = regs->r[1],
		.arg2 = regs->r[2],
		.arg3 = regs->r[3],
		.arg4 = regs->r[4],
		.arg5 = regs->r[5],
		.arg6 = regs->r[6],
		.arg7 = regs->r[7],
	};
}

void arch_cpu_init(struct cpu *c, ipaddr_t entry_point)
{
	plat_psci_cpu_resume(c, entry_point);

	/*
	 * Linux expects LORegions to be disabled, hence if the current system
	 * supports them, Peregrine ensures that they are disabled.
	 */
	lor_disable();

	write_msr(CPTR_EL2, get_cptr_el2_value());

	/* Initialize counter-timer virtual offset register to 0. */
	write_msr(CNTVOFF_EL2, 0);

	if(c->id == 0) {
		init_gic();
	}
	// TODO: add barrier such that all CPU start only after GIC init is finished

	plat_interrupts_controller_hw_init(c);
}
