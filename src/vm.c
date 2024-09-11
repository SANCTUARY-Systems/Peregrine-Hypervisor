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

#include "pg/vm.h"

#include "pg/api.h"
#include "pg/check.h"
#include "pg/cpu.h"
#include "pg/layout.h"
#include "pg/plat/iommu.h"
#include "pg/std.h"
#include "pg/dlog.h"
#include "pg/pma.h"
#include "pg/error.h"

#include "vmapi/pg/call.h"

static struct vm vms[MAX_VMS];
static uint16_t  vm_count;
static struct vm *first_boot_vm;

struct vm *vm_init(uint16_t id,
                   uint16_t vcpu_count,
                   uint16_t pcpu_count,
                   uint32_t *vm_cpus,
                   struct mpool *ppool)
{
    struct vm  *vm;
    struct cpu *cpu;
    bool       ans;     /* answer */

    /* sanity checks */
    RET(vcpu_count > MAX_CPUS, NULL,
        "vCPUs assigned to VM exceed limit of %u\n", MAX_CPUS);
    RET(vcpu_count > pcpu_count, NULL,
        "Not enough physical CPUs assigned to VM\n");

    /* Check VM ID */
	uint16_t vm_index = id - PG_VM_ID_OFFSET;

	RET(id < PG_VM_ID_OFFSET, NULL, "Invalid VM ID: %u\n", id);
	RET(vm_index > ARRAY_SIZE(vms), NULL,
		"VM index out of bounds: %u", vm_index);

	vm = &vms[vm_index];

    memset_s(vm, sizeof(*vm), 0, sizeof(*vm));

    list_init(&vm->mailbox.waiter_list);
    list_init(&vm->mailbox.ready_list);
    sl_init(&vm->lock);

    vm->id = id;
    vm->vcpu_count = vcpu_count;
    vm->mailbox.state = MAILBOX_STATE_EMPTY;
    atomic_init(&vm->aborting, false);

    ans = mm_vm_init(&vm->ptable, ppool);
    RET(!ans, NULL, "Unable to initialize VM page table\n");

    /* initialise waiter entries */
    for (size_t i = 0; i < MAX_VMS; i++) {
        vm->wait_entries[i].waiting_vm = vm;
        list_init(&vm->wait_entries[i].wait_links);
        list_init(&vm->wait_entries[i].ready_links);
    }

    /* do basic initialization of vCPUs */
    for (size_t i = 0; i < vcpu_count; i++) {
        ans = vcpu_init(vm_get_vcpu(vm, i), vm);
        RET(!ans, NULL, "Unable to do basic initialization of vCPU %u", i);
    }

    /* assign physical CPUs to VM                           *
     * NOTE: vCPU - pCPU mapping should be provided by user *
     *       vCPU scheduling on pCPUs not yet supported     */
    for (size_t i = 0; i < vcpu_count; i++) {
        cpu = cpu_find(vm_cpus[i]);
        RET(!cpu, NULL, "Unable to find CPU %#x\n", vm_cpus[i]);

        vm->cpus[i] = cpu->id;
        cpu->is_assigned = true;
        dlog_debug("Assigned CPU %#x to VM %u (%u / %u)\n",
                   vm->cpus[i], vm->id, i + 1, vcpu_count);
    }

    /* WARNING: At this point, the vCPUs are initialized and the "cpus" *
     *          component of the VM structure contains the assigned     *
     *          physical CPUS. However, these are not yet bound to the  *
     *          "vcpus" elements. For that, check cpu_main().           */

    return vm;
}

/* vm_init_next - creates and initializes a new VM object
 *  @vcpu_count : Number of virtual CPUs
 *  @pcpu_count : Number of physical CPUs (must be >= to vcpu_count)
 *  @cpus       : Array of IDs of physical CPU assigned to current VM
 *  @ppool      : Memory pool
 *  @vm         : Reference to a VM object pointer that will be initialized
 *
 *  @return : true if everything went well; false otherwise
 */
bool
vm_init_next(uint16_t vcpu_count,
             uint16_t pcpu_count,
             uint32_t *cpus,
             struct mpool *ppool,
             struct vm **new_vm)
{
    /* sanity check */
    RET(vm_count >= MAX_VMS, false, "Too many VMs initialized\n");
    RET(vcpu_count > pcpu_count, false, "vCPU scheduling not yet supported\n");

    /* initialize new VM */
    *new_vm = vm_init(vm_count + PG_VM_ID_OFFSET,
                      vcpu_count, pcpu_count, cpus, ppool);
    RET(*new_vm == NULL, false, "Unable to initialize VM %u\n", vm_count);

    vm_count++;
    return true;
}

uint16_t vm_get_count(void)
{
	return vm_count;
}

/**
 * Returns a pointer to the VM with the corresponding id.
 */
struct vm *vm_find(uint16_t id)
{
	uint16_t index;

	/* Check that this is not a reserved ID. */
	if (id < PG_VM_ID_OFFSET) {
		return NULL;
	}

	index = id - PG_VM_ID_OFFSET;

	return vm_find_index(index);
}

/**
 * Returns a pointer to the VM with the corresponding uuid.
*/
struct vm *vm_find_uuid(uuid_t *uuid)
{
	uint16_t max_init_vms = vm_count > MAX_VMS ? MAX_VMS : vm_count; // play it safe - makes static analysis happier

	for (int i = 0; i < max_init_vms; i++)
	{
		if (uuid_is_equal(uuid, &vms[i].uuid))
		{
			return &vms[i];
		}
	}

	return NULL;
}

/**
 * Returns a pointer to the VM at the specified index.
 */
struct vm *vm_find_index(uint16_t index)
{
	/* Ensure the VM is initialized. */
	if (index >= vm_count || index >= MAX_VMS) {  // play it safe - makes static analysis happier
		return NULL;
	}

	return &vms[index];
}

/**
 * Find VM which belongs to the current CPU.
 * We check in the PSCI interface that VMs can only issue calls to CPUs assigned to them.
 */
struct vm *vm_find_from_cpu(struct cpu *cpu)
{
	struct vm *vm_loop = NULL;
	struct vm *vm = NULL;

	if (cpu->id == 0x0) {
		vm = vm_get_first_boot();
	} else {
		for (int i = 0; i < vm_get_count(); i++) {
			vm_loop = vm_find_index(i);

			for (int a = 0; a < vm_loop->vcpu_count; a++) {
				if (vm_loop->cpus[a] == cpu->id) {
					vm = vm_loop;
					return vm;
				}
			}
		}
	}
	return vm;
}

/* vm_local_cpu_index - Translates physical CPU to VM's vCPU index
 *  @cpu : Physical CPU object
 *
 *  @return : vCPU index on success; (uint16_t) -1 if not assigned to any VM
 *
 * TODO: This replaces Emmanuel's old function. Since we know the VM every time,
 *       do we really need to iterate over the entire VM list?
 */
uint16_t
vm_local_cpu_index(struct cpu *cpu)
{
    struct vm *vm;

    for (size_t i = 0; i < vm_get_count(); i++) {
        vm = vm_find_index(i);
        RET(!vm, -1, "Unable to get reference to VM %u\n", i);

        for (size_t j = 0; j < vm->vcpu_count; j++)
            if (vm->cpus[j] == cpu->id)
                return j;
    }

    return -1;
}

/**
 * Locks the given VM and updates `locked` to hold the newly locked VM.
 */
struct vm_locked vm_lock(struct vm *vm)
{
	struct vm_locked locked = {
		.vm = vm,
	};

	sl_lock(&vm->lock);

	return locked;
}

/**
 * Locks two VMs ensuring that the locking order is according to the locks'
 * addresses.
 */
struct two_vm_locked vm_lock_both(struct vm *vm1, struct vm *vm2)
{
	struct two_vm_locked dual_lock;

	sl_lock_both(&vm1->lock, &vm2->lock);
	dual_lock.vm1.vm = vm1;
	dual_lock.vm2.vm = vm2;

	return dual_lock;
}

/**
 * Unlocks a VM previously locked with vm_lock, and updates `locked` to reflect
 * the fact that the VM is no longer locked.
 */
void vm_unlock(struct vm_locked *locked)
{
	sl_unlock(&locked->vm->lock);
	locked->vm = NULL;
}

/**
 * Get the vCPU with the given index from the given VM.
 * This assumes the index is valid, i.e. less than vm->vcpu_count.
 */
struct vcpu *vm_get_vcpu(struct vm *vm, uint16_t vcpu_index)
{
	CHECK(vm->vcpu_count <= MAX_CPUS);
	CHECK(vcpu_index < vm->vcpu_count);
	return &vm->vcpus[vcpu_index];
}

/**
 * Gets `vm`'s wait entry for waiting on the `for_vm`.
 */
struct wait_entry *vm_get_wait_entry(struct vm *vm, uint16_t for_vm)
{
	uint16_t index;

	CHECK(for_vm >= PG_VM_ID_OFFSET);
	index = for_vm - PG_VM_ID_OFFSET;
	CHECK(index < MAX_VMS);

	return &vm->wait_entries[index];
}

/**
 * Gets the ID of the VM which the given VM's wait entry is for.
 */
uint16_t vm_id_for_wait_entry(struct vm *vm, struct wait_entry *entry)
{
	uint16_t index = entry - vm->wait_entries;

	return index + PG_VM_ID_OFFSET;
}

/**
 * Map a range of addresses to the VM in both the MMU and the IOMMU.
 *
 * mm_vm_defrag should always be called after a series of page table updates,
 * whether they succeed or fail. This is because on failure extra page table
 * entries may have been allocated and then not used, while on success it may be
 * possible to compact the page table by merging several entries into a block.
 *
 * Returns true on success, or false if the update failed and no changes were
 * made.
 *
 */
bool vm_identity_map(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
		     uint32_t mode, struct mpool *ppool, ipaddr_t *ipa)
{
	dlog_debug("vm_identity_map: %#x - %#x\n", begin.pa, end.pa);
	if (!vm_identity_prepare(vm_locked, begin, end, mode, ppool)) {
		return false;
	}

	vm_identity_commit(vm_locked, begin, end, mode, ppool, ipa);

	return true;
}

/**
 * Map a range of addresses to the VM in both the MMU and the IOMMU.
 *
 * mm_vm_defrag should always be called after a series of page table updates,
 * whether they succeed or fail. This is because on failure extra page table
 * entries may have been allocated and then not used, while on success it may be
 * possible to compact the page table by merging several entries into a block.
 *
 * Returns true on success, or false if the update failed and no changes were
 * made.
 * 
 * TODO: currently there is no unmapping that relates to this function,
 * but since it is only used for freeram memory this is not needed currently.
 *
 */
bool vm_identity_map_and_reserve(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
		     uint32_t mode, struct mpool *ppool, ipaddr_t *ipa)
{
	dlog_debug("vm_identity_map_and_reserve: %#x - %#x\n", begin.pa, end.pa);
	if (!vm_identity_prepare(vm_locked, begin, end, mode, ppool)) {
		return false;
	}

	if(!pma_reserve_memory(begin.pa, end.pa, vm_locked.vm->id)){
		return false;
	}

	vm_identity_commit(vm_locked, begin, end, mode, ppool, ipa);

	return true;
}

/**
 * Prepares the given VM for the given address mapping such that it will be able
 * to commit the change without failure.
 *
 * In particular, multiple calls to this function will result in the
 * corresponding calls to commit the changes to succeed.
 *
 * Returns true on success, or false if the update failed and no changes were
 * made.
 */
bool vm_identity_prepare(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			 uint32_t mode, struct mpool *ppool)
{
	return mm_vm_prepare(&vm_locked.vm->ptable, ipa_from_pa(begin), begin, end, mode,
				      ppool);
}

/**
 * Commits the given address mapping to the VM assuming the operation cannot
 * fail. `vm_identity_prepare` must used correctly before this to ensure
 * this condition.
 */
void vm_identity_commit(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
			uint32_t mode, struct mpool *ppool, ipaddr_t *ipa)
{
	mm_vm_commit(&vm_locked.vm->ptable, ipa_from_pa(begin), begin, end, mode, ppool,
			      ipa);
	plat_iommu_identity_map(vm_locked, begin, end, mode);
}

/**
 * Unmap a range of addresses from the VM.
 *
 * Returns true on success, or false if the update failed and no changes were
 * made.
 */
bool vm_unmap(struct vm_locked vm_locked, paddr_t begin, paddr_t end,
	      struct mpool *ppool)
{
	uint32_t mode = MM_MODE_UNMAPPED_MASK;

	return vm_identity_map(vm_locked, begin, end, mode, ppool, NULL);
}

/**
 * Unmaps the hypervisor pages from the given page table.
 */
bool vm_unmap_hypervisor(struct vm_locked vm_locked, struct mpool *ppool)
{
	/* TODO: If we add pages dynamically, they must be included here too. */
	return vm_unmap(vm_locked, layout_text_begin(), layout_text_end(),
			ppool) &&
	       vm_unmap(vm_locked, layout_rodata_begin(), layout_rodata_end(),
			ppool) &&
	       vm_unmap(vm_locked, layout_data_begin(), layout_data_end(),
			ppool);
}

/**
 * Gets the first partition to boot, according to Boot Protocol from FFA spec.
 */
struct vm *vm_get_first_boot(void)
{
	return first_boot_vm;
}

/**
 * Insert in boot list, sorted by `boot_order` parameter in the vm structure
 * and rooted in `first_boot_vm`.
 */
void vm_update_boot(struct vm *vm)
{
	struct vm *current = NULL;
	struct vm *previous = NULL;

	if (first_boot_vm == NULL) {
		first_boot_vm = vm;
		return;
	}

	current = first_boot_vm;

	while (current != NULL && current->boot_order >= vm->boot_order) {
		previous = current;
		current = current->next_boot;
	}

	if (previous != NULL) {
		previous->next_boot = vm;
	} else {
		first_boot_vm = vm;
	}

	vm->next_boot = current;
}
