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

#include <stdnoreturn.h>

#include "pg/arch/barriers.h"
#include "pg/arch/init.h"
#include "pg/arch/mmu.h"
#include "pg/arch/plat/smc.h"
#include "pg/arch/tee/mediator.h"
#include "pg/arch/virtioac.h"
#include "pg/arch/emulator.h"
#include "pg/arch/virt_devs.h"

#include "pg/api.h"
#include "pg/check.h"
#include "pg/cpu.h"
#include "pg/dlog.h"
#include "pg/ffa.h"
#include "pg/ffa_internal.h"
#include "pg/panic.h"
#include "pg/plat/interrupts.h"
#include "pg/interrupt_desc.h"
#include "pg/vm.h"

#include "vmapi/pg/call.h"

#include "debug_el1.h"
#include "feature_id.h"
#include "msr.h"
#include "perfmon.h"
#include "psci.h"
#include "psci_handler.h"
#include "smc.h"
#include "sysregs.h"

/**
 * Hypervisor Fault Address Register Non-Secure.
 */
#define HPFAR_EL2_NS (UINT64_C(0x1) << 63)

/**
 * Hypervisor Fault Address Register Faulting IPA.
 */
#define HPFAR_EL2_FIPA (UINT64_C(0xFFFFFFFFFF0))

/**
 * Gets the value to increment for the next PC.
 * The ESR encodes whether the instruction is 2 bytes or 4 bytes long.
 */
#define GET_NEXT_PC_INC(esr) (GET_ESR_IL(esr) ? 4 : 2)

/**
 * The Client ID field within X7 for an SMC64 call.
 */
#define CLIENT_ID_MASK UINT64_C(0xffff)

/**
 * Returns a reference to the currently executing vCPU.
 */
static struct vcpu *current(void)
{
	return (struct vcpu *)read_msr(tpidr_el2);
}

/* New: moved interrupt delegation into handler */

/**
 * returns the vCPU by the interrupt owning VM targeted
 * by the interrupt.
 */
static struct vcpu *find_target_vcpu(struct vcpu *current,
					      uint32_t interrupt_id)
{
	bool target_vm_found = false;
	struct vm *vm;
	struct vcpu *target_vcpu = NULL;
	struct interrupt_descriptor int_desc;

	/**
	 * Find which VM owns this interrupt. We then find the corresponding
	 * vCPU context for this CPU.
	 */
	for (uint16_t index = 0; index < vm_get_count(); ++index) {
		vm = vm_find_index(index);

		for (uint32_t j = 0; j < PG_NUM_INTIDS; j++) {
			int_desc = vm->interrupt_desc[j];

			/** Interrupt descriptors are populated contiguously. */
			if (!int_desc.valid) {
				break;
			}
			if (int_desc.interrupt_id == interrupt_id) {
				target_vm_found = true;
				break;
			}
		}
		if (target_vm_found) {
			break;
		}
	}

	CHECK(target_vm_found);

	/* codechecker_false_positive [cppcheck-uninitvar] the check for target_vm_found prevents vm from being uninitialized */
	target_vcpu = api_get_vm_vcpu(vm_find(vm->id), current);

	CHECK(target_vcpu != NULL);

	return target_vcpu;
}

/**
 * NOTE: this is from WIP Hafnium upstream
 * 
 * TODO: As of now, we did not implement support for checking legal state
 * transitions defined under the partition runtime models defined in the
 * FF-A v1.1-Beta spec.
 * Moreover, support for scheduling models has not been implemented. However,
 * the current implementation loosely maps to the following valid actions for
 * a S-EL1 Partition:

 Runtime Model		NS-Int			Self S-Int	Other S-Int
 --------------------------------------------------------------------------
 Message Processing	Signalable with ME	Signalable	Signalable
 Interrupt Handling	Queued			Queued		Queued
 --------------------------------------------------------------------------
 */

/**
 * Delegate the interrupt handling to the target vcpu.
 */
static void delegate_interrupt(struct vcpu *current, struct vcpu **next)
{
	int64_t ret;
	uint32_t id;
	struct vcpu_locked target_vcpu_locked;
	struct vcpu_locked current_vcpu_locked;
	struct vcpu *target_vcpu;

	/* Find pending interrupt id. This also activates the interrupt. */
	id = plat_interrupts_get_pending_interrupt_id();

	target_vcpu = find_target_vcpu(current, id);

	/* Update the state of current vCPU. */
	current_vcpu_locked = vcpu_lock(current);
	current->state = VCPU_STATE_PREEMPTED;
	vcpu_unlock(&current_vcpu_locked);

	/**
	 *  TODO: set priority mask back after interrupt 
	 * was handled with plat_interrupts_set_priority_mask(0xff);
	 * 
	 *	TODO: Temporarily mask all interrupts to disallow high priority
	 * interrupts from pre-empting current interrupt processing.
	 */
	plat_interrupts_set_priority_mask(0x0);

	target_vcpu_locked = vcpu_lock(target_vcpu);

	/* Inject this interrupt as a vIRQ to the target VM context. */
	ret = api_interrupt_inject_locked(target_vcpu_locked, id, current,
					  NULL);

	/* Ideally should not happen unless a programming error. */
	if (ret == 1) {
		/*
		 * We know execution was preempted while running in secure
		 * world. SPM will resume execution in target vCPU.
		 */
		panic("PVM should not schedule target vCPU\n");
	}

	/**
	 * Special scenario where target vCPU is the current vCPU in normal
	 * world.
	 */
	if (current == target_vcpu) {
		dlog_verbose("Resume current vCPU\n");
		*next = NULL;

		/* We have already locked vCPU. */
		current->state = VCPU_STATE_RUNNING;
	} else {
		/* Switch to target vCPU responsible for this interrupt. */
		*next = target_vcpu;

		struct ffa_value args = {
			.func = (uint32_t)FFA_INTERRUPT_32,
		};

		if (target_vcpu->state == VCPU_STATE_READY) {
			args.arg1 = id;
		} else if (target_vcpu->state == VCPU_STATE_BLOCKED_MAILBOX) {
			/* Implementation defined mechanism. */
			args.arg1 = DEFERRED_INT_ID;
		} else if (target_vcpu->state == VCPU_STATE_PREEMPTED ||
			   target_vcpu->state == VCPU_STATE_BLOCKED_INTERRUPT) {
			/*
			 * We do not resume a target vCPU that has been already
			 * pre-empted by an interrupt or waiting for an
			 * interrupt(WFI). We only pend the vIRQ for target SP
			 * and continue to resume current vCPU.
			 */
			*next = NULL;
			goto out;
			/*
			 * TODO: Can we do better with the vCPU that is in
			 * STATE_BLOCKED_INTERRUPT ? Perhaps immediately resume
			 * it?
			 */
		} else {
			/*
			 * vCPU of Target VM cannot be in RUNNING/OFF/ABORTED
			 * state if it has to handle secure interrupt.
			 */
			panic("Secure interrupt cannot be signaled to target "
			      "VM\n");
		}

		CHECK((*next)->regs_available);
		arch_regs_set_retval(&((*next)->regs), args);
		(*next)->state = VCPU_STATE_RUNNING;

		/* Mark the registers as unavailable now. */
		(*next)->regs_available = false;
	}
out:
	//target_vcpu->processing_secure_interrupt = true;

	// if (target_vcpu == current) {
	// 	/**
	// 	 * In scenario where target vCPU is the current vCPU in
	// 	 * normal world, there is no vCPU to resume when target
	// 	 * vCPU exits after interrupt completion.
	// 	 */
	// 	target_vcpu->preempted_vcpu = NULL;
	// } else {
	// 	target_vcpu->preempted_vcpu = current;
	// }

	//target_vcpu->current_sec_interrupt_id = id;
	vcpu_unlock(&target_vcpu_locked);
}

/**
 * Saves the state of per-vCPU peripherals, such as the virtual timer, and
 * informs the arch-independent sections that registers have been saved.
 */
void complete_saving_state(struct vcpu *vcpu)
{
	if (has_vhe_support()) {
		vcpu->regs.peripherals.cntv_cval_el0 =
			read_msr(MSR_CNTV_CVAL_EL02);
		vcpu->regs.peripherals.cntv_ctl_el0 =
			read_msr(MSR_CNTV_CTL_EL02);
	} else {
		vcpu->regs.peripherals.cntv_cval_el0 = read_msr(cntv_cval_el0);
		vcpu->regs.peripherals.cntv_ctl_el0 = read_msr(cntv_ctl_el0);
	}

	api_regs_state_saved(vcpu);

	/*
	 * If switching away from the primary, copy the current EL0 virtual
	 * timer registers to the corresponding EL2 physical timer registers.
	 * This is used to emulate the virtual timer for the primary in case it
	 * should fire while the secondary is running.
	 */
	if (vcpu->vm->id == PG_PRIMARY_VM_ID) {
		/*
		 * Clear timer control register before copying compare value, to
		 * avoid a spurious timer interrupt. This could be a problem if
		 * the interrupt is configured as edge-triggered, as it would
		 * then be latched in.
		 */
		write_msr(cnthp_ctl_el2, 0);

		if (has_vhe_support()) {
			write_msr(cnthp_cval_el2, read_msr(MSR_CNTV_CVAL_EL02));
			write_msr(cnthp_ctl_el2, read_msr(MSR_CNTV_CTL_EL02));
		} else {
			write_msr(cnthp_cval_el2, read_msr(cntv_cval_el0));
			write_msr(cnthp_ctl_el2, read_msr(cntv_ctl_el0));
		}
	}
}

/**
 * Restores the state of per-vCPU peripherals, such as the virtual timer.
 */
void begin_restoring_state(struct vcpu *vcpu)
{
	/*
	 * Clear timer control register before restoring compare value, to avoid
	 * a spurious timer interrupt. This could be a problem if the interrupt
	 * is configured as edge-triggered, as it would then be latched in.
	 */
	if (has_vhe_support()) {
		write_msr(MSR_CNTV_CTL_EL02, 0);
		write_msr(MSR_CNTV_CVAL_EL02,
			  vcpu->regs.peripherals.cntv_cval_el0);
		write_msr(MSR_CNTV_CTL_EL02,
			  vcpu->regs.peripherals.cntv_ctl_el0);
	} else {
		write_msr(cntv_ctl_el0, 0);
		write_msr(cntv_cval_el0, vcpu->regs.peripherals.cntv_cval_el0);
		write_msr(cntv_ctl_el0, vcpu->regs.peripherals.cntv_ctl_el0);
	}

	/*
	 * If we are switching (back) to the primary, disable the EL2 physical
	 * timer which was being used to emulate the EL0 virtual timer, as the
	 * virtual timer is now running for the primary again.
	 */
	if (vcpu->vm->id == PG_PRIMARY_VM_ID) {
		write_msr(cnthp_ctl_el2, 0);
		write_msr(cnthp_cval_el2, 0);
	}
}

/**
 * Invalidate all stage 1 TLB entries on the current (physical) CPU for the
 * current VMID.
 */
static void invalidate_vm_tlb(void)
{
	/*
	 * Ensure that the last VTTBR write has taken effect so we invalidate
	 * the right set of TLB entries.
	 */
	isb();

	__asm__ volatile("tlbi vmalle1");

	/*
	 * Ensure that no instructions are fetched for the VM until after the
	 * TLB invalidation has taken effect.
	 */
	isb();

	/*
	 * Ensure that no data reads or writes for the VM happen until after the
	 * TLB invalidation has taken effect. Non-shareable is enough because
	 * the TLB is local to the CPU.
	 */
	dsb(nsh);
}

/**
 * Invalidates the TLB if a different vCPU is being run than the last vCPU of
 * the same VM which was run on the current pCPU.
 *
 * This is necessary because VMs may (contrary to the architecture
 * specification) use inconsistent ASIDs across vCPUs. c.f. KVM's similar
 * workaround:
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=94d0e5980d6791b9
 */
void maybe_invalidate_tlb(struct vcpu *vcpu)
{
	size_t   current_cpu_index = cpu_index(vcpu->cpu);
	uint16_t new_vcpu_index    = vcpu_index(vcpu);

	if (vcpu->vm->arch.last_vcpu_on_cpu[current_cpu_index] !=
	    new_vcpu_index) {
		/*
		 * The vCPU has changed since the last time this VM was run on
		 * this pCPU, so we need to invalidate the TLB.
		 */
		invalidate_vm_tlb();

		/* Record the fact that this vCPU is now running on this CPU. */
		vcpu->vm->arch.last_vcpu_on_cpu[current_cpu_index] =
			new_vcpu_index;
	}
}

noreturn void irq_current_exception_noreturn(uintreg_t elr, uintreg_t spsr)
{
	(void)elr;
	(void)spsr;

	panic("IRQ from current exception level.");
}

noreturn void fiq_current_exception_noreturn(uintreg_t elr, uintreg_t spsr)
{
	(void)elr;
	(void)spsr;

	panic("FIQ from current exception level.");
}

noreturn void serr_current_exception_noreturn(uintreg_t elr, uintreg_t spsr)
{
	(void)elr;
	(void)spsr;

	panic("SError from current exception level.");
}

noreturn void sync_current_exception_noreturn(uintreg_t elr, uintreg_t spsr)
{
	uintreg_t esr = read_msr(esr_el2);
	uintreg_t ec = GET_ESR_EC(esr);

	(void)spsr;

	switch (ec) {
	case EC_DATA_ABORT_SAME_EL:
		if (!(esr & (1U << 10))) { /* Check FnV bit. */
			dlog_error(
				"Data abort: pc=%#x, esr=%#x, ec=%#x, "
				"far=%#x\n",
				elr, esr, ec, read_msr(far_el2));
		} else {
			dlog_error(
				"Data abort: pc=%#x, esr=%#x, ec=%#x, "
				"far=invalid\n",
				elr, esr, ec);
		}

		break;

	default:
		dlog_error(
			"Unknown current sync exception pc=%#x, esr=%#x, "
			"ec=%#x\n",
			elr, esr, ec);
		break;
	}

	panic("EL2 exception");
}

/**
 * Sets or clears the VI bit in the HCR_EL2 register saved in the given
 * arch_regs.
 */
static void set_virtual_irq(struct arch_regs *r, bool enable)
{
	if (enable) {
		r->hcr_el2 |= HCR_EL2_VI;
	} else {
		r->hcr_el2 &= ~HCR_EL2_VI;
	}
}

/**
 * Sets or clears the VI bit in the HCR_EL2 register.
 */
static void set_virtual_irq_current(bool enable)
{
	uintreg_t hcr_el2 = current()->regs.hcr_el2;

	if (enable) {
		hcr_el2 |= HCR_EL2_VI;
	} else {
		hcr_el2 &= ~HCR_EL2_VI;
	}
	current()->regs.hcr_el2 = hcr_el2;
}

/**
 * Sets or clears the VF bit in the HCR_EL2 register saved in the given
 * arch_regs.
 */
static void set_virtual_fiq(struct arch_regs *r, bool enable)
{
	if (enable) {
		r->hcr_el2 |= HCR_EL2_VF;
	} else {
		r->hcr_el2 &= ~HCR_EL2_VF;
	}
}

/**
 * Sets or clears the VF bit in the HCR_EL2 register.
 */
static void set_virtual_fiq_current(bool enable)
{
	uintreg_t hcr_el2 = current()->regs.hcr_el2;

	if (enable) {
		hcr_el2 |= HCR_EL2_VF;
	} else {
		hcr_el2 &= ~HCR_EL2_VF;
	}
	current()->regs.hcr_el2 = hcr_el2;
}

/**
 * Checks whether to block an SMC being forwarded from a VM.
 */
static bool smc_is_blocked(const struct vm *vm, uint32_t func)
{
	bool block_by_default = !vm->smc_whitelist.permissive;

	for (size_t i = 0; i < vm->smc_whitelist.smc_count; ++i) {
		if (func == vm->smc_whitelist.smcs[i]) {
			return false;
		}
	}

	if(block_by_default){
		dlog_warning("SMC %#010x attempted from VM %#x got blocked\n", func,
				vm->id);
	}

	/* Access is still allowed in permissive mode. */
	return block_by_default;
}

/**
 * Applies SMC access control according to manifest and forwards the call if
 * access is granted.
 */
static void smc_forwarder(const struct vm *vm, struct ffa_value *args)
{
	struct ffa_value ret = {0};
	uint32_t client_id = vm->id;
	uintreg_t arg7 = args->arg7;

	if (smc_is_blocked(vm, args->func)) {
		args->func = SMCCC_ERROR_UNKNOWN;
		return;
	}

	/*
	 * Set the Client ID but keep the existing Secure OS ID and anything
	 * else (currently unspecified) that the client may have passed in the
	 * upper bits.
	 */
	args->arg7 = client_id | (arg7 & ~CLIENT_ID_MASK);


	memcpy_s(&ret, sizeof(struct ffa_value), args, sizeof(struct ffa_value));

	if (!tee_mediator_ops.handle_smccc(args, &ret)) {
		ret = smc_forward(args->func, args->arg1, args->arg2,
				     args->arg3, args->arg4, args->arg5,
				     args->arg6, args->arg7);
	}

	/*
	 * Preserve the value passed by the caller, rather than the generated
	 * client_id. Note that this would also overwrite any return value that
	 * may be in x7, but the SMCs that we are forwarding are legacy calls
	 * from before SMCCC 1.2 so won't have more than 4 return values anyway.
	 */
	ret.arg7 = arg7;

	plat_smc_post_forward(*args, &ret);

	*args = ret;
}

/**
 * Set or clear VI/VF bits according to pending interrupts.
 */
static void vcpu_update_virtual_interrupts(struct vcpu *next)
{
	struct vcpu_locked vcpu_locked;

	if (next == NULL) {
		/*
		 * Not switching vCPUs, set the bit for the current vCPU
		 * directly in the register.
		 */

		vcpu_locked = vcpu_lock(current());
		set_virtual_irq_current(
			vcpu_interrupt_irq_count_get(vcpu_locked) > 0);
		set_virtual_fiq_current(
			vcpu_interrupt_fiq_count_get(vcpu_locked) > 0);
		vcpu_unlock(&vcpu_locked);
	} else {
		/*
		 * About to switch vCPUs, set the bit for the vCPU to which we
		 * are switching in the saved copy of the register.
		 */

		vcpu_locked = vcpu_lock(next);
		set_virtual_irq(&next->regs,
				vcpu_interrupt_irq_count_get(vcpu_locked) > 0);
		set_virtual_fiq(&next->regs,
				vcpu_interrupt_fiq_count_get(vcpu_locked) > 0);
		vcpu_unlock(&vcpu_locked);
	}
}

/**
 * Handles PSCI and FF-A calls and writes the return value back to the registers
 * of the vCPU. This is shared between smc_handler and hvc_handler.
 *
 * Returns true if the call was handled.
 */
static bool hvc_smc_handler(struct ffa_value args, struct vcpu *vcpu,
			    struct vcpu **next)
{
	/* Do not expect PSCI calls emitted from within the secure world. */
	if (psci_handler(vcpu, args.func, args.arg1, args.arg2, args.arg3,
			 &vcpu->regs.r[0], next)) {
		return true;
	}

	return false;
}

/**
 * Processes SMC instruction calls.
 */
static struct vcpu *smc_handler(struct vcpu *vcpu)
{
	struct ffa_value args = arch_regs_get_args(&vcpu->regs);
	struct vcpu *next = NULL;


	if (hvc_smc_handler(args, vcpu, &next)) {
			return next;
	}

	smc_forwarder(vcpu->vm, &args);
	arch_regs_set_retval(&vcpu->regs, args);
	return NULL;
}

/*
 * Exception vector offsets.
 * See Arm Architecture Reference Manual Armv8-A, D1.10.2.
 */

/**
 * Offset for synchronous exceptions at current EL with SPx.
 */
#define OFFSET_CURRENT_SPX UINT64_C(0x200)

/**
 * Offset for synchronous exceptions at lower EL using AArch64.
 */
#define OFFSET_LOWER_EL_64 UINT64_C(0x400)

/**
 * Offset for synchronous exceptions at lower EL using AArch32.
 */
#define OFFSET_LOWER_EL_32 UINT64_C(0x600)

/**
 * Returns the address for the exception handler at EL1.
 */
static uintreg_t get_el1_exception_handler_addr(const struct vcpu *vcpu)
{
	uintreg_t base_addr = has_vhe_support() ? read_msr(MSR_VBAR_EL12)
						: read_msr(vbar_el1);
	uintreg_t pe_mode = vcpu->regs.spsr & PSR_PE_MODE_MASK;
	bool is_arch32 = vcpu->regs.spsr & PSR_ARCH_MODE_32;

	if (pe_mode == PSR_PE_MODE_EL0T) {
		if (is_arch32) {
			base_addr += OFFSET_LOWER_EL_32;
		} else {
			base_addr += OFFSET_LOWER_EL_64;
		}
	} else {
		CHECK(!is_arch32);
		base_addr += OFFSET_CURRENT_SPX;
	}

	return base_addr;
}

/**
 * Injects an exception with the specified Exception Syndrom Register value into
 * the EL1.
 *
 * NOTE: This function assumes that the lazy registers haven't been saved, and
 * writes to the lazy registers of the CPU directly instead of the vCPU.
 */
static void inject_el1_exception(struct vcpu *vcpu, uintreg_t esr_el1_value,
				 uintreg_t far_el1_value)
{
	uintreg_t handler_address = get_el1_exception_handler_addr(vcpu);

	/* Update the CPU state to inject the exception. */
	if (has_vhe_support()) {
		write_msr(MSR_ESR_EL12, esr_el1_value);
		write_msr(MSR_FAR_EL12, far_el1_value);
		write_msr(MSR_ELR_EL12, vcpu->regs.pc);
		write_msr(MSR_SPSR_EL12, vcpu->regs.spsr);
	} else {
		write_msr(esr_el1, esr_el1_value);
		write_msr(far_el1, far_el1_value);
		write_msr(elr_el1, vcpu->regs.pc);
		write_msr(spsr_el1, vcpu->regs.spsr);
	}

	/*
	 * Mask (disable) interrupts and run in EL1h mode.
	 * EL1h mode is used because by default, taking an exception selects the
	 * stack pointer for the target Exception level. The software can change
	 * that later in the handler if needed.
	 */
	vcpu->regs.spsr = PSR_D | PSR_A | PSR_I | PSR_F | PSR_PE_MODE_EL1H;

	/* Transfer control to the exception hander. */
	vcpu->regs.pc = handler_address;
}

/**
 * Injects a Data Abort exception (same exception level).
 */
static void inject_el1_data_abort_exception(struct vcpu *vcpu,
					    uintreg_t esr_el2,
					    uintreg_t far_el2)
{
	/*
	 *  ISS encoding remains the same, but the EC is changed to reflect
	 *  where the exception came from.
	 *  See Arm Architecture Reference Manual Armv8-A, pages D13-2943/2982.
	 */
	uintreg_t esr_el1_value = GET_ESR_ISS(esr_el2) | GET_ESR_IL(esr_el2) |
				  (EC_DATA_ABORT_SAME_EL << ESR_EC_OFFSET);

	dlog_notice("Injecting Data Abort exception into VM %#x.\n",
		    vcpu->vm->id);

	inject_el1_exception(vcpu, esr_el1_value, far_el2);
}

/**
 * Injects a Data Abort exception (same exception level).
 */
static void inject_el1_instruction_abort_exception(struct vcpu *vcpu,
						   uintreg_t esr_el2,
						   uintreg_t far_el2)
{
	/*
	 *  ISS encoding remains the same, but the EC is changed to reflect
	 *  where the exception came from.
	 *  See Arm Architecture Reference Manual Armv8-A, pages D13-2941/2980.
	 */
	uintreg_t esr_el1_value =
		GET_ESR_ISS(esr_el2) | GET_ESR_IL(esr_el2) |
		(EC_INSTRUCTION_ABORT_SAME_EL << ESR_EC_OFFSET);

	dlog_notice("Injecting Instruction Abort exception into VM %#x.\n",
		    vcpu->vm->id);

	inject_el1_exception(vcpu, esr_el1_value, far_el2);
}

/**
 * Injects an exception with an unknown reason into the EL1.
 */
static void inject_el1_unknown_exception(struct vcpu *vcpu, uintreg_t esr_el2)
{
	uintreg_t esr_el1_value =
		GET_ESR_IL(esr_el2) | (EC_UNKNOWN << ESR_EC_OFFSET);

	/*
	 * The value of the far_el2 register is UNKNOWN in this case,
	 * therefore, don't propagate it to avoid leaking sensitive information.
	 */
	uintreg_t far_el1_value = 0;
	char *direction_str;

	direction_str = ISS_IS_READ(esr_el2) ? "read" : "write";
	dlog_notice(
		"Trapped access to system register %s: op0=%d, op1=%d, crn=%d, "
		"crm=%d, op2=%d, rt=%d.\n",
		direction_str, GET_ISS_OP0(esr_el2), GET_ISS_OP1(esr_el2),
		GET_ISS_CRN(esr_el2), GET_ISS_CRM(esr_el2),
		GET_ISS_OP2(esr_el2), GET_ISS_RT(esr_el2));

	dlog_notice("Injecting Unknown Reason exception into VM %#x.\n",
		    vcpu->vm->id);

	inject_el1_exception(vcpu, esr_el1_value, far_el1_value);
}

static struct vcpu *hvc_handler(struct vcpu *vcpu)
{
	struct ffa_value args = arch_regs_get_args(&vcpu->regs);
	struct vcpu *next = NULL;

	if (hvc_smc_handler(args, vcpu, &next)) {
		return next;
	}

	switch (args.func) {
	case PG_INTERRUPT_ENABLE:
		vcpu->regs.r[0] = api_interrupt_enable(args.arg1, args.arg2,
						       args.arg3, vcpu);
		break;

	case PG_INTERRUPT_GET:
		vcpu->regs.r[0] = api_interrupt_get(vcpu);
		break;

	case PG_INTERRUPT_INJECT:
		vcpu->regs.r[0] = api_interrupt_inject(args.arg1, args.arg2,
						       args.arg3, vcpu, &next);
		break;

	default:
		vcpu->regs.r[0] = SMCCC_ERROR_UNKNOWN;
	}

	vcpu_update_virtual_interrupts(next);

	return next;
}

struct vcpu *irq_lower(void)
{
	/* New: we handle interrupts in the hypervisor instead of primary VM */
	struct vcpu *vcpu = current();
	struct vcpu *target_vcpu = NULL;
	delegate_interrupt(vcpu, &target_vcpu);

	/*
	 * Since we are in interrupt context, set the bit for the
	 * next vCPU directly in the register.
	 */
	vcpu_update_virtual_interrupts(target_vcpu);

	return target_vcpu;
	
	// /* Old:
	//  * Switch back to primary VM, interrupts will be handled there.
	//  *
	//  * If the VM has aborted, this vCPU will be aborted when the scheduler
	//  * tries to run it again. This means the interrupt will not be delayed
	//  * by the aborted VM.
	//  *
	//  * TODO: Only switch when the interrupt isn't for the current VM.
	//  */
	// return api_preempt(current());
}

struct vcpu *fiq_lower(void)
{
	return irq_lower();
}

noreturn struct vcpu *serr_lower(void)
{
	/*
	 * SError exceptions should be isolated and handled by the responsible
	 * VM/exception level. Getting here indicates a bug, that isolation is
	 * not working, or a processor that does not support ARMv8.2-IESB, in
	 * which case Hafnium routes SError exceptions to EL2 (here).
	 */
	panic("SError from a lower exception level.");
}

/**
 * Initialises a fault info structure. It assumes that an FnV bit exists at
 * bit offset 10 of the ESR, and that it is only valid when the bottom 6 bits of
 * the ESR (the fault status code) are 010000; this is the case for both
 * instruction and data aborts, but not necessarily for other exception reasons.
 */
static struct vcpu_fault_info fault_info_init(uintreg_t esr,
					      const struct vcpu *vcpu,
					      uint32_t mode)
{
	uint32_t fsc = esr & 0x3f;
	struct vcpu_fault_info r;
	uint64_t hpfar_el2_val;
	uint64_t hpfar_el2_fipa;

	r.mode = mode;
	r.pc = va_init(vcpu->regs.pc);

	/* Get Hypervisor IPA Fault Address value. */
	hpfar_el2_val = read_msr(hpfar_el2);

	/* Extract Faulting IPA. */
	hpfar_el2_fipa = (hpfar_el2_val & HPFAR_EL2_FIPA) << 8;

	/*
	 * Check the FnV bit, which is only valid if dfsc/ifsc is 010000. It
	 * indicates that we cannot rely on far_el2.
	 */
	if (fsc == 0x10 && esr & (1U << 10)) {
		r.vaddr = va_init(0);
		r.ipaddr = ipa_init(hpfar_el2_fipa);
	} else {
		r.vaddr = va_init(read_msr(far_el2));
		r.ipaddr = ipa_init(hpfar_el2_fipa |
				    (read_msr(far_el2) & (PAGE_SIZE - 1)));
	}

	return r;
}

struct vcpu *sync_lower_exception(uintreg_t esr, uintreg_t far)
{
	struct vcpu *vcpu = current();
	struct vcpu_fault_info info;
	struct vcpu *new_vcpu = vcpu;
	uintreg_t ec = GET_ESR_EC(esr);

	switch (ec) {
	case EC_WFI_WFE:
		/* Skip the instruction. */
		vcpu->regs.pc += GET_NEXT_PC_INC(esr);
		/* Check TI bit of ISS, 0 = WFI, 1 = WFE. */
		if (esr & 1) {
			/* WFE */
			/*
			 * TODO: consider giving the scheduler more context,
			 * somehow.
			 */
			api_yield(vcpu, &new_vcpu);
			return new_vcpu;
		}
		/* WFI */
		return api_wait_for_interrupt(vcpu);

	case EC_DATA_ABORT_LOWER_EL:
		info = fault_info_init(
			esr, vcpu, (esr & (1U << 6)) ? MM_MODE_W : MM_MODE_R);

		if((info.ipaddr.ipa >= VIRTIO_START) && (info.ipaddr.ipa <= VIRTIO_END)) {
		    if(virtioac_handle(esr, far, GET_NEXT_PC_INC(esr), vcpu, &info)) {
		        return NULL;
		    }
		}
		else
		{
            /* try to resolve this as a virtual device access */
            if (access_virt_dev(esr, far, GET_NEXT_PC_INC(esr), vcpu, &info)) {
                return NULL;
            }

            /* try to resolve this as a GIC access */
            if (access_gicv3(esr, far, GET_NEXT_PC_INC(esr), vcpu, &info)) {
                return NULL;
            }

			dlog_warning("Data Abort | PC:%#x IPA:%#x\n",
				vcpu->regs.pc,
				info.ipaddr.ipa);
		}
		if (vcpu_handle_page_fault(vcpu, &info)) {
			return NULL;
		}
		/* Inform the EL1 of the data abort. */
		inject_el1_data_abort_exception(vcpu, esr, far);

		/* Schedule the same VM to continue running. */
		return NULL;

	case EC_INSTRUCTION_ABORT_LOWER_EL:
		info = fault_info_init(esr, vcpu, MM_MODE_X);
		if (vcpu_handle_page_fault(vcpu, &info)) {
			return NULL;
		}
		/* Inform the EL1 of the instruction abort. */
		inject_el1_instruction_abort_exception(vcpu, esr, far);

		/* Schedule the same VM to continue running. */
		return NULL;

	case EC_HVC:
		return hvc_handler(vcpu);

	case EC_SMC: {
		uintreg_t smc_pc = vcpu->regs.pc;
		struct vcpu *next = smc_handler(vcpu);

		/* Skip the SMC instruction. */
		vcpu->regs.pc = smc_pc + GET_NEXT_PC_INC(esr);

		return next;
	}

	case EC_MSR:
		/*
		 * NOTE: This should never be reached because it goes through a
		 * separate path handled by handle_system_register_access().
		 */
		panic("Handled by handle_system_register_access().");

	default:
		dlog_notice(
			"Unknown lower sync exception pc=%#x, esr=%#x, "
			"ec=%#x\n",
			vcpu->regs.pc, esr, ec);
		break;
	}

	/*
	 * The exception wasn't handled. Inject to the VM to give it chance to
	 * handle as an unknown exception.
	 */
	inject_el1_unknown_exception(vcpu, esr);

	/* Schedule the same VM to continue running. */
	return NULL;
}

/**
 * Handles EC = 011000, MSR, MRS instruction traps.
 * Returns non-null ONLY if the access failed and the vCPU is changing.
 */
void handle_system_register_access(uintreg_t esr_el2)
{
	struct vcpu *vcpu = current();
	uint16_t vm_id = vcpu->vm->id;
	uintreg_t ec = GET_ESR_EC(esr_el2);

	CHECK(ec == EC_MSR);
	/*
	 * Handle accesses to debug and performance monitor registers.
	 * Inject an exception for unhandled/unsupported registers.
	 */
	if (debug_el1_is_register_access(esr_el2)) {
		if (!debug_el1_process_access(vcpu, vm_id, esr_el2)) {
			inject_el1_unknown_exception(vcpu, esr_el2);
			return;
		}
	} else if (perfmon_is_register_access(esr_el2)) {
		if (!perfmon_process_access(vcpu, vm_id, esr_el2)) {
			inject_el1_unknown_exception(vcpu, esr_el2);
			return;
		}
	} else if (feature_id_is_register_access(esr_el2)) {
		if (!feature_id_process_access(vcpu, esr_el2)) {
			inject_el1_unknown_exception(vcpu, esr_el2);
			return;
		}
	} else if (icc_icv_is_register_access(esr_el2)) {
		if (!icc_icv_process_access(vcpu, esr_el2)) {
			inject_el1_unknown_exception(vcpu, esr_el2);
			return;
		}
	} else if (is_cache_maintenance(esr_el2)) {
		if (!process_cache_maintenance(vcpu, esr_el2)) {
			inject_el1_unknown_exception(vcpu, esr_el2);
			return;
		}
	} else {
		inject_el1_unknown_exception(vcpu, esr_el2);
		return;
	}

	/* Instruction was fulfilled. Skip it and run the next one. */
	vcpu->regs.pc += GET_NEXT_PC_INC(esr_el2);
}
