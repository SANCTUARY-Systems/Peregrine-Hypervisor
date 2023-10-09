/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/sve.h"

#include "pg/cpu.h"
#include "pg/offset_size_header.h"
#include "pg/vm.h"

DEFINE_SIZEOF(CPU_SIZE, struct cpu)

DEFINE_OFFSETOF(CPU_ID, struct cpu, id)
DEFINE_OFFSETOF(CPU_STACK_BOTTOM, struct cpu, stack_bottom)
DEFINE_OFFSETOF(VCPU_VM, struct vcpu, vm)
DEFINE_OFFSETOF(VCPU_CPU, struct vcpu, cpu)
DEFINE_OFFSETOF(VCPU_REGS, struct vcpu, regs)
DEFINE_OFFSETOF(VCPU_LAZY, struct vcpu, regs.lazy)
DEFINE_OFFSETOF(VCPU_FREGS, struct vcpu, regs.fp)
#if BRANCH_PROTECTION
DEFINE_OFFSETOF(VCPU_PAC, struct vcpu, regs.pac)
#endif

DEFINE_OFFSETOF(VM_ID, struct vm, id)

#if GIC_VERSION == 3 || GIC_VERSION == 4
DEFINE_OFFSETOF(VCPU_GIC, struct vcpu, regs.gic)
#endif

DEFINE_SIZEOF(SVE_CTX_SIZE, struct sve_context_t)
DEFINE_OFFSETOF(SVE_CTX_FFR, struct sve_context_t, ffr)
DEFINE_OFFSETOF(SVE_CTX_PREDICATES, struct sve_context_t, predicates)
