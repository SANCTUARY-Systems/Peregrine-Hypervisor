/*
 * Copyright 2019 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/vcpu.h"

#include "pg/arch/cpu.h"

#include "pg/check.h"
#include "pg/dlog.h"
#include "pg/std.h"
#include "pg/vm.h"

/**
 * Locks the given vCPU and updates `locked` to hold the newly locked vCPU.
 */
struct vcpu_locked vcpu_lock(struct vcpu *vcpu)
{
	struct vcpu_locked locked = {
		.vcpu = vcpu,
	};

	sl_lock(&vcpu->lock);

	return locked;
}

/**
 * Locks two vCPUs ensuring that the locking order is according to the locks'
 * addresses.
 */
struct two_vcpu_locked vcpu_lock_both(struct vcpu *vcpu1, struct vcpu *vcpu2)
{
	struct two_vcpu_locked dual_lock;

	sl_lock_both(&vcpu1->lock, &vcpu2->lock);
	dual_lock.vcpu1.vcpu = vcpu1;
	dual_lock.vcpu2.vcpu = vcpu2;

	return dual_lock;
}

/**
 * Unlocks a vCPU previously locked with vpu_lock, and updates `locked` to
 * reflect the fact that the vCPU is no longer locked.
 */
void vcpu_unlock(struct vcpu_locked *locked)
{
	sl_unlock(&locked->vcpu->lock);
	locked->vcpu = NULL;
}

bool vcpu_init(struct vcpu *vcpu, struct vm *vm)
{
	//bool assign_error = true;
	memset_s(vcpu, sizeof(*vcpu), 0, sizeof(*vcpu));
	sl_init(&vcpu->lock);
	vcpu->regs_available = true;
	vcpu->vm = vm;
	vcpu->state = VCPU_STATE_OFF;

	return true;
}

/**
 * Initialise the registers for the given vCPU and set the state to
 * VCPU_STATE_READY. The caller must hold the vCPU lock while calling this.
 */
void vcpu_on(struct vcpu_locked vcpu, ipaddr_t entry, uintreg_t arg)
{
	arch_regs_set_pc_arg(&vcpu.vcpu->regs, entry, arg);
	vcpu.vcpu->state = VCPU_STATE_READY;
}

uint16_t vcpu_index(const struct vcpu *vcpu)
{
	size_t index = vcpu - vcpu->vm->vcpus;

	CHECK(index < UINT16_MAX);
	return index;
}

/**
 * Check whether the given vcpu_state is an off state, for the purpose of
 * turning vCPUs on and off. Note that aborted still counts as on in this
 * context.
 */
bool vcpu_is_off(struct vcpu_locked vcpu)
{
	switch (vcpu.vcpu->state) {
	case VCPU_STATE_OFF:
		return true;
	case VCPU_STATE_READY:
	case VCPU_STATE_RUNNING:
	case VCPU_STATE_BLOCKED_MAILBOX:
	case VCPU_STATE_BLOCKED_INTERRUPT:
	case VCPU_STATE_PREEMPTED:
	case VCPU_STATE_ABORTED:
	default:
		/*
		 * Aborted still counts as ON for the purposes of PSCI,
		 * because according to the PSCI specification (section
		 * 5.7.1) a core is only considered to be off if it has
		 * been turned off with a CPU_OFF call or hasn't yet
		 * been turned on with a CPU_ON call.
		 */
		return false;
	}
}

/**
 * Starts a vCPU of a secondary VM.
 *
 * Returns true if the secondary was reset and started, or false if it was
 * already on and so nothing was done.
 */
bool vcpu_secondary_reset_and_start(struct vcpu_locked vcpu_locked,
				    ipaddr_t entry, uintreg_t arg)
{
	struct vm *vm = vcpu_locked.vcpu->vm;
	bool vcpu_was_off;

	CHECK(vm->id != PG_PRIMARY_VM_ID);

	vcpu_was_off = vcpu_is_off(vcpu_locked);
	if (vcpu_was_off) {
		/*
		 * Set vCPU registers to a clean state ready for boot. As this
		 * is a secondary which can migrate between pCPUs, the ID of the
		 * vCPU is defined as the index and does not match the ID of the
		 * pCPU it is running on.
		 */
		arch_regs_reset(vcpu_locked.vcpu);
		vcpu_on(vcpu_locked, entry, arg);
	}

	return vcpu_was_off;
}

/**
 * Handles a page fault. It does so by determining if it's a legitimate or
 * spurious fault, and recovering from the latter.
 *
 * Returns true if the caller should resume the current vCPU, or false if its VM
 * should be aborted.
 */
bool vcpu_handle_page_fault(const struct vcpu *current,
			    struct vcpu_fault_info *f)
{
	struct vm *vm = current->vm;
	uint32_t mode;
	uint32_t mask = f->mode | MM_MODE_INVALID;
	bool resume;

	sl_lock(&vm->lock);

	/*
	 * Check if this is a legitimate fault, i.e., if the page table doesn't
	 * allow the access attempted by the VM.
	 *
	 * Otherwise, this is a spurious fault, likely because another CPU is
	 * updating the page table. It is responsible for issuing global TLB
	 * invalidations while holding the VM lock, so we don't need to do
	 * anything else to recover from it. (Acquiring/releasing the lock
	 * ensured that the invalidations have completed.)
	 */
	resume = mm_vm_get_mode(&vm->ptable, f->ipaddr, ipa_add(f->ipaddr, 1),
				&mode) &&
		 (mode & mask) == f->mode;

	sl_unlock(&vm->lock);

	if (!resume) {
		dlog_warning(
			"Stage-2 page fault: pc=%#x, vmid=%#x, vcpu=%u, "
			"vaddr=%#x, ipaddr=%#x, mode=%#x\n",
			f->pc, vm->id, vcpu_index(current), f->vaddr, f->ipaddr,
			f->mode);
	}

	return resume;
}

void vcpu_reset(struct vcpu *vcpu)
{
	arch_cpu_init(vcpu->cpu, vcpu->vm->secondary_ep);

	/* Reset the registers to give a clean start for vCPU. */
	arch_regs_reset(vcpu);
}
