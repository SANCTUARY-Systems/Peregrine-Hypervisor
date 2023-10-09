/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include "pg/cpu.h"
#include "pg/mpool.h"
#include "pg/vm.h"

#include "vmapi/pg/call.h"
#include "vmapi/pg/ffa.h"


struct vcpu *api_get_vm_vcpu(struct vm *vm, struct vcpu *current);
void api_regs_state_saved(struct vcpu *vcpu);

struct vcpu *api_wait_for_interrupt(struct vcpu *current);
struct vcpu *api_vcpu_off(struct vcpu *current);
struct vcpu *api_wake_up(struct vcpu *current, struct vcpu *target_vcpu);

int64_t api_interrupt_enable(uint32_t intid, bool enable,
			     enum interrupt_type type, struct vcpu *current);
uint32_t api_interrupt_get(struct vcpu *current);
int64_t api_interrupt_inject(uint16_t target_vm_id,
			     uint16_t target_vcpu_idx, uint32_t intid,
			     struct vcpu *current, struct vcpu **next);
int64_t api_interrupt_inject_locked(struct vcpu_locked target_locked,
				    uint32_t intid, struct vcpu *current,
				    struct vcpu **next);

struct ffa_value api_vm_configure_pages(
	struct mm_stage1_locked mm_stage1_locked, struct vm_locked vm_locked,
	ipaddr_t send, ipaddr_t recv, uint32_t page_count,
	struct mpool *local_page_pool);
struct ffa_value api_yield(struct vcpu *current, struct vcpu **next);
