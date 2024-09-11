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
 *     Copyright 2019 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "psci_handler.h"

#include <stdint.h>

#include "pg/arch/plat/psci.h"
#include "pg/arch/types.h"

#include "pg/api.h"
#include "pg/cpu.h"
#include "pg/dlog.h"
#include "pg/ffa.h"
#include "pg/panic.h"
#include "pg/vm.h"
#include "pg/dlog.h"
#include "pg/arch/emulator.h"

#include "psci.h"

void cpu_entry(struct cpu *c);


/**
 * Check whether the caller VM is allowed to perform PSCI calls for the target CPU.
 */
cpu_id_t psci_check_permission(struct vm *vm, uintreg_t cpu_id)
{
//OPTION1: cpu_ids: 0x0, 0x100, 0x200, ...
	uint32_t cpu_no = aff_to_no(cpu_id);

	if(cpu_no < vm->vcpu_count)
	{
		return vm->cpus[cpu_no];
	}

//OPTION2: cpu_ids 0x0, real-id, real-id, ...
//	if(cpu_id == 0x0)
//	{
//		return vm->cpus[0];
//	}
//	else
//	{
//		for(uint32_t i = 1; i < MAX_CPUS; i++)
//		{
//			if(vm->cpus[i] == cpu_id)
//			{
//				return cpu_id;
//			}
//		}
//	}

	return CPU_ERROR_INVALID_ID;
}

/**
 * Handles PSCI requests received via HVC or SMC instructions from the primary
 * VM.
 *
 * A minimal PSCI 1.1 interface is offered which can make use of the
 * implementation of PSCI in EL3 by acting as an adapter.
 *
 * Returns true if the request was a PSCI one, false otherwise.
 */
bool psci_primary_vm_handler(struct vcpu *vcpu, uint32_t func, uintreg_t arg0,
			     uintreg_t arg1, uintreg_t arg2, uintreg_t *ret)
{
	cpu_id_t cpu_id;
	struct cpu *c;
	struct ffa_value smc_res;

	/*
	 * If there's a problem with the EL3 PSCI, block standard secure service
	 * calls by marking them as unknown. Other calls will be allowed to pass
	 * through.
	 *
	 * This blocks more calls than just PSCI so it may need to be made more
	 * lenient in future.
	 */
	if (plat_psci_version_get() == 0) {
		*ret = SMCCC_ERROR_UNKNOWN;
		return (func & SMCCC_SERVICE_CALL_MASK) ==
		       SMCCC_STANDARD_SECURE_SERVICE_CALL;
	}

	switch (func & ~SMCCC_CONVENTION_MASK) {
	case PSCI_VERSION:
		*ret = PSCI_VERSION_1_1;
		break;

	case PSCI_FEATURES:
		switch (arg0 & ~SMCCC_CONVENTION_MASK) {
		case PSCI_CPU_SUSPEND:
			if (plat_psci_version_get() == PSCI_VERSION_0_2) {
				/*
				 * PSCI 0.2 doesn't support PSCI_FEATURES so
				 * report PSCI 0.2 compatible features.
				 */
				*ret = 0;
			} else {
				/* PSCI 1.x only defines two feature bits. */
				smc_res = smc32(func, arg0, 0, 0, 0, 0, 0,
						SMCCC_CALLER_HYPERVISOR);
				*ret = smc_res.func & 0x3;
			}
			break;

		case PSCI_VERSION:
		case PSCI_FEATURES:
		case PSCI_SYSTEM_OFF:
		case PSCI_SYSTEM_RESET:
		case PSCI_AFFINITY_INFO:
		case PSCI_CPU_OFF:
		case PSCI_CPU_ON:
			/* These are supported without special features. */
			*ret = 0;
			break;

		default:
			/* Everything else is unsupported. */
			*ret = PSCI_ERROR_NOT_SUPPORTED;
			break;
		}
		break;

	case PSCI_SYSTEM_OFF:
		smc32(PSCI_SYSTEM_OFF, 0, 0, 0, 0, 0, 0,
		      SMCCC_CALLER_HYPERVISOR);
		panic("System off failed");
		break;

	case PSCI_SYSTEM_RESET:
		smc32(PSCI_SYSTEM_RESET, 0, 0, 0, 0, 0, 0,
		      SMCCC_CALLER_HYPERVISOR);
		panic("System reset failed");
		break;

	case PSCI_AFFINITY_INFO:

		/* Verify VM permission */
		cpu_id = psci_check_permission(vcpu->vm, arg0);
		if (cpu_id == CPU_ERROR_INVALID_ID) {
			dlog_warning("VM not allowed to issue the PSCI call: 0x%x", PSCI_AFFINITY_INFO);
			*ret = PSCI_ERROR_NO_PERMISSION;
			break;
		}

		c = cpu_find(cpu_id);
		if (!c) {
			*ret = PSCI_ERROR_INVALID_PARAMETERS;
			break;
		}

		if (arg1 != 0) {
			*ret = PSCI_ERROR_NOT_SUPPORTED;
			break;
		}

		sl_lock(&c->lock);
		if (c->is_on) {
			*ret = PSCI_RETURN_ON;
		} else {
			*ret = PSCI_RETURN_OFF;
		}
		sl_unlock(&c->lock);
		break;

	case PSCI_CPU_SUSPEND: {

		/* Verify VM permission */
		cpu_id = psci_check_permission(vcpu->vm, arg0);
		if (cpu_id == CPU_ERROR_INVALID_ID) {
			dlog_warning("VM not allowed to issue the PSCI call: 0x%x", PSCI_CPU_SUSPEND);
			*ret = PSCI_ERROR_NO_PERMISSION;
			break;
		}

		plat_psci_cpu_suspend(cpu_id);
		/*
		 * Update vCPU state to wake from the provided entry point but
		 * if suspend returns, for example because it failed or was a
		 * standby power state, the SMC will return and the updated
		 * vCPU registers will be ignored.
		 */
		arch_regs_set_pc_arg(&vcpu->regs, ipa_init(arg1), arg2);
		smc_res = smc64(PSCI_CPU_SUSPEND, cpu_id, (uintreg_t)&cpu_entry,
				(uintreg_t)vcpu->cpu, 0, 0, 0,
				SMCCC_CALLER_HYPERVISOR);
		*ret = smc_res.func;
		break;
	}

	case PSCI_CPU_OFF:

		/* Verify VM permission */
		cpu_id = psci_check_permission(vcpu->vm, arg0);
		if (cpu_id == CPU_ERROR_INVALID_ID) {
			dlog_warning("VM not allowed to issue the PSCI call: 0x%x", PSCI_CPU_OFF);
			*ret = PSCI_ERROR_NO_PERMISSION;
			break;
		}

		cpu_off(vcpu->cpu);
		smc32(PSCI_CPU_OFF, 0, 0, 0, 0, 0, 0, SMCCC_CALLER_HYPERVISOR);
		panic("CPU off failed");
		break;

	case PSCI_CPU_ON:
		dlog_debug("PSCI_HANDLER PSCI_CPU_ON VM: 0x%x, func: 0x%x, arg0: 0x%x, arg1: 0x%x, arg2: 0x%x\n",
				vcpu->vm->id, func, arg0, arg1, arg2);

		/* Verify VM permission */
		cpu_id = psci_check_permission(vcpu->vm, arg0);
		if (cpu_id == CPU_ERROR_INVALID_ID) {
			dlog_warning("VM not allowed to issue the PSCI call: 0x%x\n", PSCI_CPU_ON);
			*ret = PSCI_ERROR_NO_PERMISSION;
			break;
		}

		c = cpu_find(cpu_id);
		if (!c) {
			*ret = PSCI_ERROR_INVALID_PARAMETERS;
			break;
		}

		if (cpu_on(c, ipa_init(arg1), arg2)) {
			*ret = PSCI_ERROR_ALREADY_ON;
			break;
		}

		/*
		 * There's a race when turning a CPU on when it's in the
		 * process of turning off. We need to loop here while it is
		 * reported that the CPU is on (because it's about to turn
		 * itself off).
		 */
		do {
			smc_res = smc64(PSCI_CPU_ON, cpu_id,
					(uintreg_t)&cpu_entry, (uintreg_t)c, 0,
					0, 0, SMCCC_CALLER_HYPERVISOR);
			*ret = smc_res.func;
		} while (*ret == (uintreg_t)PSCI_ERROR_ALREADY_ON);

		if (*ret != PSCI_RETURN_SUCCESS) {
			cpu_off(c);
		}
		break;

	case PSCI_MIGRATE:
	case PSCI_MIGRATE_INFO_TYPE:
	case PSCI_MIGRATE_INFO_UP_CPU:
	case PSCI_CPU_FREEZE:
	case PSCI_CPU_DEFAULT_SUSPEND:
	case PSCI_NODE_HW_STATE:
	case PSCI_SYSTEM_SUSPEND:
	case PSCI_SET_SYSPEND_MODE:
	case PSCI_STAT_RESIDENCY:
	case PSCI_STAT_COUNT:
	case PSCI_SYSTEM_RESET2:
	case PSCI_MEM_PROTECT:
	case PSCI_MEM_PROTECT_CHECK_RANGE:
		/* Block all other known PSCI calls. */
		*ret = PSCI_ERROR_NOT_SUPPORTED;
		break;

	default:
		return false;
	}

	return true;
}

/**
 * Convert a PSCI CPU / affinity ID for a secondary VM to the corresponding vCPU
 * index.
 */
uint16_t vcpu_id_to_index(cpu_id_t vcpu_id)
{
	/* For now we use indices as IDs for the purposes of PSCI. */
	return vcpu_id;
}

/**
 * Handles PSCI requests received via HVC or SMC instructions from a secondary
 * VM.
 *
 * A minimal PSCI 1.1 interface is offered which can start and stop vCPUs in
 * collaboration with the scheduler in the primary VM.
 *
 * Returns true if the request was a PSCI one, false otherwise.
 */
bool psci_secondary_vm_handler(struct vcpu *vcpu, uint32_t func, uintreg_t arg0,
			       uintreg_t arg1, uintreg_t arg2, uintreg_t *ret,
			       struct vcpu **next)
{
	switch (func & ~SMCCC_CONVENTION_MASK) {
	case PSCI_VERSION:
		*ret = PSCI_VERSION_1_1;
		break;

	case PSCI_FEATURES:
		switch (arg0 & ~SMCCC_CONVENTION_MASK) {
		case PSCI_CPU_SUSPEND:
			/*
			 * Does not offer OS-initiated mode but does use
			 * extended StateID Format.
			 */
			*ret = 0x2;
			break;

		case PSCI_VERSION:
		case PSCI_FEATURES:
		case PSCI_AFFINITY_INFO:
		case PSCI_CPU_OFF:
		case PSCI_CPU_ON:
			/* These are supported without special features. */
			*ret = 0;
			break;

		default:
			/* Everything else is unsupported. */
			*ret = PSCI_ERROR_NOT_SUPPORTED;
			break;
		}
		break;

	case PSCI_AFFINITY_INFO: {
		cpu_id_t target_affinity = arg0;
		uint32_t lowest_affinity_level = arg1;
		struct vm *vm = vcpu->vm;
		struct vcpu_locked target_vcpu;
		uint16_t target_vcpu_index =
			vcpu_id_to_index(target_affinity);

		if (lowest_affinity_level != 0) {
			/* Affinity levels greater than 0 not supported. */
			*ret = PSCI_ERROR_INVALID_PARAMETERS;
			break;
		}

		if (target_vcpu_index >= vm->vcpu_count) {
			*ret = PSCI_ERROR_INVALID_PARAMETERS;
			break;
		}

		target_vcpu = vcpu_lock(vm_get_vcpu(vm, target_vcpu_index));
		*ret = vcpu_is_off(target_vcpu) ? PSCI_RETURN_OFF
						: PSCI_RETURN_ON;
		vcpu_unlock(&target_vcpu);
		break;
	}

	case PSCI_CPU_SUSPEND: {
		/*
		 * Downgrade suspend request to WFI and return SUCCESS, as
		 * allowed by the specification.
		 */
		*next = api_wait_for_interrupt(vcpu);
		*ret = PSCI_RETURN_SUCCESS;
		break;
	}

	case PSCI_CPU_OFF:
		/*
		 * Should never return to the caller, but in case it somehow
		 * does.
		 */
		*ret = PSCI_ERROR_DENIED;
		/* Tell the scheduler not to run the vCPU again. */
		*next = api_vcpu_off(vcpu);
		break;

	case PSCI_CPU_ON: {
		/* Parameter names as per PSCI specification. */
		cpu_id_t target_cpu = arg0;
		ipaddr_t entry_point_address = ipa_init(arg1);
		uint64_t context_id = arg2;
		uint16_t target_vcpu_index =
			vcpu_id_to_index(target_cpu);
		struct vm *vm = vcpu->vm;
		struct vcpu *target_vcpu;
		struct vcpu_locked vcpu_locked;
		bool vcpu_was_off;

		if (target_vcpu_index >= vm->vcpu_count) {
			*ret = PSCI_ERROR_INVALID_PARAMETERS;
			break;
		}

		target_vcpu = vm_get_vcpu(vm, target_vcpu_index);
		vcpu_locked = vcpu_lock(target_vcpu);
		vcpu_was_off = vcpu_secondary_reset_and_start(
			vcpu_locked, entry_point_address, context_id);
		vcpu_unlock(&vcpu_locked);

		if (vcpu_was_off) {
			/*
			 * Tell the scheduler that it can start running the new
			 * vCPU now.
			 */
			*next = api_wake_up(vcpu, target_vcpu);
			*ret = PSCI_RETURN_SUCCESS;
		} else {
			*ret = PSCI_ERROR_ALREADY_ON;
		}

		break;
	}

	case PSCI_SYSTEM_OFF:
	case PSCI_SYSTEM_RESET:
	case PSCI_MIGRATE:
	case PSCI_MIGRATE_INFO_TYPE:
	case PSCI_MIGRATE_INFO_UP_CPU:
	case PSCI_CPU_FREEZE:
	case PSCI_CPU_DEFAULT_SUSPEND:
	case PSCI_NODE_HW_STATE:
	case PSCI_SYSTEM_SUSPEND:
	case PSCI_SET_SYSPEND_MODE:
	case PSCI_STAT_RESIDENCY:
	case PSCI_STAT_COUNT:
	case PSCI_SYSTEM_RESET2:
	case PSCI_MEM_PROTECT:
	case PSCI_MEM_PROTECT_CHECK_RANGE:
		/* Block all other known PSCI calls. */
		*ret = PSCI_ERROR_NOT_SUPPORTED;
		break;

	default:
		return false;
	}

	return true;
}

/**
 * Handles PSCI requests received via HVC or SMC instructions from a VM.
 * Requests from primary and secondary VMs are dealt with differently.
 *
 * Returns true if the request was a PSCI one, false otherwise.
 */
bool psci_handler(struct vcpu *vcpu, uint32_t func, uintreg_t arg0,
		  uintreg_t arg1, uintreg_t arg2, uintreg_t *ret,
		  struct vcpu **next)
{
	if (vcpu->vm->id == PG_PRIMARY_VM_ID || vcpu->vm->id == 0x2) {
		return psci_primary_vm_handler(vcpu, func, arg0, arg1, arg2,
					       ret);
	}

	// TEST PRINT
	dlog_error("PSCI_HANDLER call for secondary VM, should never be called!\n");

	return psci_secondary_vm_handler(vcpu, func, arg0, arg1, arg2, ret,
					 next);
}

