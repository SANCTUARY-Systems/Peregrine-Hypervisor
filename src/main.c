/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/cpu.h"
#include "pg/vm.h"
#include "pg/dlog.h"
#include "pg/error.h"

/**
 * The entry point of CPUs when they are turned on. It is supposed to initialise
 * all state and return the first vCPU to run.
 */
struct vcpu *cpu_main(struct cpu *c)
{
    struct vcpu *vcpu;
    struct vm *vm;
    size_t cpu_index_local;

    /* get VM of current CPU */
    vm = vm_find_from_cpu(c);
    DIE(!vm, "CPU %#x not assigned to any VM\n", c->id);

    /* get vCPU index of physical CPU in VM's context */
    cpu_index_local = vm_local_cpu_index(c);
    DIE(cpu_index_local == (uint16_t) -1,
        "Unable to identify vCPU index of CPU %#x\n", c->id);

    dlog_info("Start vCPU %d of VM 0x%x on the physical core 0x%x\n",
              cpu_index_local, vm->id, c->id);

    /* map physical CPU to vCPU structure */
    vcpu = vm_get_vcpu(vm, cpu_index_local);
    vcpu->cpu = c;

    /* reset the registers to give a clean start for vCPU */
    vcpu_reset(vcpu);

    return vcpu;
}
