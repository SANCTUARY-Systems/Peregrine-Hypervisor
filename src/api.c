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
 *     Copyright 2021 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/api.h"

#include "pg/arch/cpu.h"
#include "pg/arch/mm.h"
#include "pg/arch/plat/ffa.h"
#include "pg/arch/timer.h"
#include "pg/arch/vm.h"

#include "pg/check.h"
#include "pg/dlog.h"
#include "pg/ffa_internal.h"
#include "pg/mm.h"
#include "pg/plat/console.h"
#include "pg/plat/interrupts.h"
#include "pg/spinlock.h"
#include "pg/static_assert.h"
#include "pg/std.h"
#include "pg/vm.h"

#include "vmapi/pg/call.h"
#include "vmapi/pg/ffa.h"

/**
 * Get target VM vCPU:
 * If VM is UP then return first vCPU.
 * If VM is MP then return vCPU whose index matches current CPU index.
 */
struct vcpu *api_get_vm_vcpu(struct vm *vm, struct vcpu *current)
{
	uint16_t current_cpu_index = cpu_index(current->cpu);
	struct vcpu *vcpu = NULL;

	if (vm->vcpu_count == 1) {
		vcpu = vm_get_vcpu(vm, 0);
	} else if (current_cpu_index < vm->vcpu_count) {
		vcpu = vm_get_vcpu(vm, current_cpu_index);
	}

	return vcpu;
}

/**
 * Switches the physical CPU back to the corresponding vCPU of the VM whose ID
 * is given as argument of the function.
 *
 * Called to change the context between SPs for direct messaging (when Peregrine
 * is SPMC), and on the context of the remaining 'api_switch_to_*' functions.
 *
 * This function works for partitions that are:
 * - UP migratable.
 * - MP with pinned Execution Contexts.
 */
static struct vcpu *api_switch_to_vm(struct vcpu *current,
				     struct ffa_value to_ret,
				     enum vcpu_state vcpu_state,
				     uint16_t to_id)
{
	struct vm *to_vm = vm_find(to_id);
	struct vcpu *next = api_get_vm_vcpu(to_vm, current);

	CHECK(next != NULL);

	/* Set the return value for the target VM. */
	arch_regs_set_retval(&next->regs, to_ret);

	/* Set the current vCPU state. */
	sl_lock(&current->lock);
	current->state = vcpu_state;
	sl_unlock(&current->lock);

	return next;
}

/**
 * Switches the physical CPU back to the corresponding vCPU of the primary VM.
 *
 * This triggers the scheduling logic to run. Run in the context of secondary VM
 * to cause FFA_RUN to return and the primary VM to regain control of the CPU.
 */
static struct vcpu *api_switch_to_primary(struct vcpu *current,
					  struct ffa_value primary_ret,
					  enum vcpu_state secondary_state)
{
	/*
	 * If the secondary is blocked but has a timer running, sleep until the
	 * timer fires rather than indefinitely.
	 */
	switch (primary_ret.func) {
	case PG_FFA_RUN_WAIT_FOR_INTERRUPT:
	case FFA_MSG_WAIT_32: {
		if (arch_timer_enabled_current()) {
			uint64_t remaining_ns =
				arch_timer_remaining_ns_current();

			if (remaining_ns == 0) {
				/*
				 * Timer is pending, so the current vCPU should
				 * be run again right away.
				 */
				primary_ret.func = FFA_INTERRUPT_32;
				/*
				 * primary_ret.arg1 should already be set to the
				 * current VM ID and vCPU ID.
				 */
				primary_ret.arg2 = 0;
			} else {
				primary_ret.arg2 = remaining_ns;
			}
		} else {
			primary_ret.arg2 = FFA_SLEEP_INDEFINITE;
		}
		break;
	}

	default:
		/* Do nothing. */
		break;
	}

	return api_switch_to_vm(current, primary_ret, secondary_state,
				PG_PRIMARY_VM_ID);
}

/**
 * Puts the current vCPU in wait for interrupt mode, and returns to the primary
 * VM.
 */
struct vcpu *api_wait_for_interrupt(struct vcpu *current)
{
	struct ffa_value ret = {
		.func = PG_FFA_RUN_WAIT_FOR_INTERRUPT,
		.arg1 = ffa_vm_vcpu(current->vm->id, vcpu_index(current)),
	};

	return api_switch_to_primary(current, ret,
				     VCPU_STATE_BLOCKED_INTERRUPT);
}

/**
 * Puts the current vCPU in off mode, and returns to the primary VM.
 */
struct vcpu *api_vcpu_off(struct vcpu *current)
{
	struct ffa_value ret = {
		.func = PG_FFA_RUN_WAIT_FOR_INTERRUPT,
		.arg1 = ffa_vm_vcpu(current->vm->id, vcpu_index(current)),
	};

	/*
	 * Disable the timer, so the scheduler doesn't get told to call back
	 * based on it.
	 */
	arch_timer_disable_current();

	return api_switch_to_primary(current, ret, VCPU_STATE_OFF);
}

/**
 * Returns to the primary VM to allow this CPU to be used for other tasks as the
 * vCPU does not have work to do at this moment. The current vCPU is marked as
 * ready to be scheduled again.
 */
struct ffa_value api_yield(struct vcpu *current, struct vcpu **next)
{
	struct ffa_value ret = (struct ffa_value){.func = FFA_SUCCESS_32};

	if (current->vm->id == PG_PRIMARY_VM_ID) {
		/* NOOP on the primary as it makes the scheduling decisions. */
		return ret;
	}

	*next = api_switch_to_primary(
		current,
		(struct ffa_value){.func = FFA_YIELD_32,
				   .arg1 = ffa_vm_vcpu(current->vm->id,
						       vcpu_index(current))},
		VCPU_STATE_READY);

	return ret;
}

/**
 * Switches to the primary so that it can switch to the target, or kick it if it
 * is already running on a different physical CPU.
 */
struct vcpu *api_wake_up(struct vcpu *current, struct vcpu *target_vcpu)
{
	struct ffa_value ret = {
		.func = PG_FFA_RUN_WAKE_UP,
		.arg1 = ffa_vm_vcpu(target_vcpu->vm->id,
				    vcpu_index(target_vcpu)),
	};
	return api_switch_to_primary(current, ret, VCPU_STATE_READY);
}

/**
 * This function is called by the architecture-specific context switching
 * function to indicate that register state for the given vCPU has been saved
 * and can therefore be used by other pCPUs.
 */
void api_regs_state_saved(struct vcpu *vcpu)
{
	sl_lock(&vcpu->lock);
	vcpu->regs_available = true;
	sl_unlock(&vcpu->lock);
}

/**
 * Assuming that the arguments have already been checked by the caller, injects
 * a virtual interrupt of the given ID into the given target vCPU. This doesn't
 * cause the vCPU to actually be run immediately; it will be taken when the vCPU
 * is next run, which is up to the scheduler.
 *
 * Returns:
 *  - 0 on success if no further action is needed.
 *  - 1 if it was called by the primary VM and the primary VM now needs to wake
 *    up or kick the target vCPU.
 */
int64_t api_interrupt_inject_locked(struct vcpu_locked target_locked,
				    uint32_t intid, struct vcpu *current,
				    struct vcpu **next)
{
	struct vcpu *target_vcpu = target_locked.vcpu;
	uint32_t intid_index = intid / INTERRUPT_REGISTER_BITS;
	uint32_t intid_shift = intid % INTERRUPT_REGISTER_BITS;
	uint32_t intid_mask = 1U << intid_shift;
	int64_t ret = 0;

	/*
	 * We only need to change state and (maybe) trigger a virtual interrupt
	 * if it is enabled and was not previously pending. Otherwise we can
	 * skip everything except setting the pending bit.
	 */
	if (!(target_vcpu->interrupts.interrupt_enabled[intid_index] &
	      ~target_vcpu->interrupts.interrupt_pending[intid_index] &
	      intid_mask)) {
		goto out;
	}

	/* Increment the count. */
	if ((target_vcpu->interrupts.interrupt_type[intid_index] &
	     intid_mask) == ((uint32_t)INTERRUPT_TYPE_IRQ << intid_shift)) {
		vcpu_irq_count_increment(target_locked);
	} else {
		vcpu_fiq_count_increment(target_locked);
	}

	/*
	 * Only need to update state if there was not already an
	 * interrupt enabled and pending.
	 */
	if (vcpu_interrupt_count_get(target_locked) != 1) {
		goto out;
	}

	if (current->vm->id == PG_PRIMARY_VM_ID) {
		/*
		 * If the call came from the primary VM, let it know that it
		 * should run or kick the target vCPU.
		 */
		ret = 1;
	} else if (current != target_vcpu && next != NULL) {
		*next = api_wake_up(current, target_vcpu);
	}

out:
	/* Either way, make it pending. */
	target_vcpu->interrupts.interrupt_pending[intid_index] |= intid_mask;

	return ret;
}

/* Wrapper to internal_interrupt_inject with locking of target vCPU */
static int64_t internal_interrupt_inject(struct vcpu *target_vcpu,
					 uint32_t intid, struct vcpu *current,
					 struct vcpu **next)
{
	int64_t ret;
	struct vcpu_locked target_locked;

	target_locked = vcpu_lock(target_vcpu);
	ret = api_interrupt_inject_locked(target_locked, intid, current, next);
	vcpu_unlock(&target_locked);

	return ret;
}

/**
 * Check that the mode indicates memory that is valid, owned and exclusive.
 */
static bool api_mode_valid_owned_and_exclusive(uint32_t mode)
{
	return (mode & (MM_MODE_D | MM_MODE_INVALID | MM_MODE_UNOWNED |
			MM_MODE_SHARED)) == 0;
}

/**
 * Configures the hypervisor's stage-1 view of the send and receive pages.
 */
static bool api_vm_configure_stage1(struct mm_stage1_locked mm_stage1_locked,
				    struct vm_locked vm_locked,
				    paddr_t pa_send_begin, paddr_t pa_send_end,
				    paddr_t pa_recv_begin, paddr_t pa_recv_end,
				    uint32_t extra_attributes,
				    struct mpool *local_page_pool)
{
	bool ret;

	/* Map the send page as read-only in the hypervisor address space. */
	vm_locked.vm->mailbox.send =
		mm_identity_map_and_reserve(mm_stage1_locked, pa_send_begin, pa_send_end,
				MM_MODE_R | extra_attributes, local_page_pool);
	if (!vm_locked.vm->mailbox.send) {
		/* TODO: partial defrag of failed range. */
		/* Recover any memory consumed in failed mapping. */
		/* will be removed anyways */
		// mm_defrag(mm_stage1_locked, local_page_pool);
		goto fail;
	}

	/*
	 * Map the receive page as writable in the hypervisor address space. On
	 * failure, unmap the send page before returning.
	 */
	vm_locked.vm->mailbox.recv =
		mm_identity_map_and_reserve(mm_stage1_locked, pa_recv_begin, pa_recv_end,
				MM_MODE_W | extra_attributes, local_page_pool);
	if (!vm_locked.vm->mailbox.recv) {
		/* TODO: partial defrag of failed range. */
		/* Recover any memory consumed in failed mapping. */
		/* will be removed anyways */
		// mm_defrag(mm_stage1_locked, local_page_pool);
		goto fail_undo_send;
	}

	ret = true;
	goto out;

	/*
	 * The following mappings will not require more memory than is available
	 * in the local pool.
	 */
fail_undo_send:
	vm_locked.vm->mailbox.send = NULL;
	CHECK(mm_unmap(mm_stage1_locked, pa_send_begin, pa_send_end,
		       local_page_pool));

fail:
	ret = false;

out:
	return ret;
}

/**
 * Sanity checks and configures the send and receive pages in the VM stage-2
 * and hypervisor stage-1 page tables.
 *
 * Returns:
 *  - FFA_ERROR FFA_INVALID_PARAMETERS if the given addresses are not properly
 *    aligned or are the same.
 *  - FFA_ERROR FFA_NO_MEMORY if the hypervisor was unable to map the buffers
 *    due to insuffient page table memory.
 *  - FFA_ERROR FFA_DENIED if the pages are already mapped or are not owned by
 *    the caller.
 *  - FFA_SUCCESS on success if no further action is needed.
 */

struct ffa_value api_vm_configure_pages(
	struct mm_stage1_locked mm_stage1_locked, struct vm_locked vm_locked,
	ipaddr_t send, ipaddr_t recv, uint32_t page_count,
	struct mpool *local_page_pool)
{
	struct ffa_value ret;
	paddr_t pa_send_begin;
	paddr_t pa_send_end;
	paddr_t pa_recv_begin;
	paddr_t pa_recv_end;
	uint32_t orig_send_mode;
	uint32_t orig_recv_mode;
	uint32_t extra_attributes;

	/* We only allow these to be setup once. */
	if (vm_locked.vm->mailbox.send || vm_locked.vm->mailbox.recv) {
		ret = ffa_error(FFA_DENIED);
		goto out;
	}

	/* Peregrine only supports a fixed size of RX/TX buffers. */
	if (page_count != PG_MAILBOX_SIZE / FFA_PAGE_SIZE) {
		ret = ffa_error(FFA_INVALID_PARAMETERS);
		goto out;
	}

	/* Fail if addresses are not page-aligned. */
	if (!is_aligned(ipa_addr(send), PAGE_SIZE) ||
	    !is_aligned(ipa_addr(recv), PAGE_SIZE)) {
		ret = ffa_error(FFA_INVALID_PARAMETERS);
		goto out;
	}

	/* Convert to physical addresses. */
	pa_send_begin = pa_from_ipa(send);
	pa_send_end = pa_add(pa_send_begin, PG_MAILBOX_SIZE);
	pa_recv_begin = pa_from_ipa(recv);
	pa_recv_end = pa_add(pa_recv_begin, PG_MAILBOX_SIZE);

	/* Fail if the same page is used for the send and receive pages. */
	if (pa_addr(pa_send_begin) == pa_addr(pa_recv_begin)) {
		ret = ffa_error(FFA_INVALID_PARAMETERS);
		goto out;
	}

	/*
	 * Ensure the pages are valid, owned and exclusive to the VM and that
	 * the VM has the required access to the memory.
	 */
	if (!mm_vm_get_mode(&vm_locked.vm->ptable, send,
			    ipa_add(send, PAGE_SIZE), &orig_send_mode) ||
	    !api_mode_valid_owned_and_exclusive(orig_send_mode) ||
	    (orig_send_mode & MM_MODE_R) == 0 ||
	    (orig_send_mode & MM_MODE_W) == 0) {
		ret = ffa_error(FFA_DENIED);
		goto out;
	}

	if (!mm_vm_get_mode(&vm_locked.vm->ptable, recv,
			    ipa_add(recv, PAGE_SIZE), &orig_recv_mode) ||
	    !api_mode_valid_owned_and_exclusive(orig_recv_mode) ||
	    (orig_recv_mode & MM_MODE_R) == 0) {
		ret = ffa_error(FFA_DENIED);
		goto out;
	}

	/* Take memory ownership away from the VM and mark as shared. */
	if (!vm_identity_map(
		    vm_locked, pa_send_begin, pa_send_end,
		    MM_MODE_UNOWNED | MM_MODE_SHARED | MM_MODE_R | MM_MODE_W,
		    local_page_pool, NULL)) {
		ret = ffa_error(FFA_NO_MEMORY);
		goto out;
	}

	if (!vm_identity_map(vm_locked, pa_recv_begin, pa_recv_end,
			     MM_MODE_UNOWNED | MM_MODE_SHARED | MM_MODE_R,
			     local_page_pool, NULL)) {
		/* TODO: partial defrag of failed range. */
		/* Recover any memory consumed in failed mapping. */
		mm_vm_defrag(&vm_locked.vm->ptable, local_page_pool);
		goto fail_undo_send;
	}

	/* Get extra send/recv pages mapping attributes for the given VM ID. */
	extra_attributes = arch_mm_extra_attributes_from_vm(vm_locked.vm->id);

	if (!api_vm_configure_stage1(mm_stage1_locked, vm_locked, pa_send_begin,
				     pa_send_end, pa_recv_begin, pa_recv_end,
				     extra_attributes, local_page_pool)) {
		goto fail_undo_send_and_recv;
	}

	ret = (struct ffa_value){.func = FFA_SUCCESS_32};
	goto out;

fail_undo_send_and_recv:
	CHECK(vm_identity_map(vm_locked, pa_recv_begin, pa_recv_end,
			      orig_send_mode, local_page_pool, NULL));

fail_undo_send:
	CHECK(vm_identity_map(vm_locked, pa_send_begin, pa_send_end,
			      orig_send_mode, local_page_pool, NULL));
	ret = ffa_error(FFA_NO_MEMORY);

out:
	return ret;
}

/**
 * Enables or disables a given interrupt ID for the calling vCPU.
 *
 * Returns 0 on success, or -1 if the intid is invalid.
 */
int64_t api_interrupt_enable(uint32_t intid, bool enable,
			     enum interrupt_type type, struct vcpu *current)
{
	struct vcpu_locked current_locked;
	uint32_t intid_index = intid / INTERRUPT_REGISTER_BITS;
	uint32_t intid_shift = intid % INTERRUPT_REGISTER_BITS;
	uint32_t intid_mask = 1U << intid_shift;

	if (intid >= PG_NUM_INTIDS) {
		return -1;
	}

	current_locked = vcpu_lock(current);
	if (enable) {
		/*
		 * If it is pending and was not enabled before, increment the
		 * count.
		 */
		if (current->interrupts.interrupt_pending[intid_index] &
		    ~current->interrupts.interrupt_enabled[intid_index] &
		    intid_mask) {
			if ((current->interrupts.interrupt_type[intid_index] &
			     intid_mask) ==
			    ((uint32_t)INTERRUPT_TYPE_IRQ << intid_shift)) {
				vcpu_irq_count_increment(current_locked);
			} else {
				vcpu_fiq_count_increment(current_locked);
			}
		}
		current->interrupts.interrupt_enabled[intid_index] |=
			intid_mask;

		if (type == INTERRUPT_TYPE_IRQ) {
			current->interrupts.interrupt_type[intid_index] &=
				~intid_mask;
		} else if (type == INTERRUPT_TYPE_FIQ) {
			current->interrupts.interrupt_type[intid_index] |=
				intid_mask;
		}
	} else {
		/*
		 * If it is pending and was enabled before, decrement the count.
		 */
		if (current->interrupts.interrupt_pending[intid_index] &
		    current->interrupts.interrupt_enabled[intid_index] &
		    intid_mask) {
			if ((current->interrupts.interrupt_type[intid_index] &
			     intid_mask) ==
			    ((uint32_t)INTERRUPT_TYPE_IRQ << intid_shift)) {
				vcpu_irq_count_decrement(current_locked);
			} else {
				vcpu_fiq_count_decrement(current_locked);
			}
		}
		current->interrupts.interrupt_enabled[intid_index] &=
			~intid_mask;
		current->interrupts.interrupt_type[intid_index] &= ~intid_mask;
	}

	vcpu_unlock(&current_locked);
	return 0;
}

/**
 * Returns the ID of the next pending interrupt for the calling vCPU, and
 * acknowledges it (i.e. marks it as no longer pending). Returns
 * PG_INVALID_INTID if there are no pending interrupts.
 */
uint32_t api_interrupt_get(struct vcpu *current)
{
	uint8_t i;
	uint32_t first_interrupt = PG_INVALID_INTID;
	struct vcpu_locked current_locked;

	/*
	 * Find the first enabled and pending interrupt ID, return it, and
	 * deactivate it.
	 */
	current_locked = vcpu_lock(current);
	for (i = 0; i < PG_NUM_INTIDS / INTERRUPT_REGISTER_BITS; ++i) {
		uint32_t enabled_and_pending =
			current->interrupts.interrupt_enabled[i] &
			current->interrupts.interrupt_pending[i];

		if (enabled_and_pending != 0) {
			uint8_t bit_index = ctz(enabled_and_pending);
			uint32_t intid_mask = 1U << bit_index;

			/*
			 * Mark it as no longer pending and decrement the count.
			 */
			current->interrupts.interrupt_pending[i] &= ~intid_mask;

			if ((current->interrupts.interrupt_type[i] &
			     intid_mask) == ((uint32_t)INTERRUPT_TYPE_IRQ << bit_index)) {
				vcpu_irq_count_decrement(current_locked);
			} else {
				vcpu_fiq_count_decrement(current_locked);
			}

			first_interrupt =
				i * INTERRUPT_REGISTER_BITS + bit_index;
			break;
		}
	}

	vcpu_unlock(&current_locked);
	return first_interrupt;
}

/**
 * Returns whether the current vCPU is allowed to inject an interrupt into the
 * given VM and vCPU.
 */
static inline bool is_injection_allowed(uint32_t target_vm_id,
					struct vcpu *current)
{
	uint32_t current_vm_id = current->vm->id;

	/*
	 * The primary VM is allowed to inject interrupts into any VM. Secondary
	 * VMs are only allowed to inject interrupts into their own vCPUs.
	 */
	return current_vm_id == PG_PRIMARY_VM_ID ||
	       current_vm_id == target_vm_id;
}

/**
 * Injects a virtual interrupt of the given ID into the given target vCPU.
 * This doesn't cause the vCPU to actually be run immediately; it will be taken
 * when the vCPU is next run, which is up to the scheduler.
 *
 * Returns:
 *  - -1 on failure because the target VM or vCPU doesn't exist, the interrupt
 *    ID is invalid, or the current VM is not allowed to inject interrupts to
 *    the target VM.
 *  - 0 on success if no further action is needed.
 *  - 1 if it was called by the primary VM and the primary VM now needs to wake
 *    up or kick the target vCPU.
 */
int64_t api_interrupt_inject(uint16_t target_vm_id,
			     uint16_t target_vcpu_idx, uint32_t intid,
			     struct vcpu *current, struct vcpu **next)
{
	struct vcpu *target_vcpu;
	struct vm *target_vm = vm_find(target_vm_id);

	if (intid >= PG_NUM_INTIDS) {
		return -1;
	}

	if (target_vm == NULL) {
		return -1;
	}

	if (target_vcpu_idx >= target_vm->vcpu_count) {
		/* The requested vCPU must exist. */
		return -1;
	}

	if (!is_injection_allowed(target_vm_id, current)) {
		return -1;
	}

	target_vcpu = vm_get_vcpu(target_vm, target_vcpu_idx);

	dlog_verbose(
		"Injecting interrupt %u for VM %#x vCPU %u from VM %#x vCPU "
		"%u\n",
		intid, target_vm_id, target_vcpu_idx, current->vm->id,
		vcpu_index(current));
	return internal_interrupt_inject(target_vcpu, intid, current, next);
}

