/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#define STACK_SIZE (8192)

#if !defined(__ASSEMBLER__)

#include "pg/arch/cpu.h"

/* TODO: Fix alignment such that `cpu` structs are in different cache lines. */
struct cpu {
	/** CPU identifier. Doesn't have to be contiguous. */
	cpu_id_t id;

	/** Pointer to bottom of the stack. */
	void *stack_bottom;

	/** See api.c for the partial ordering on locks. */
	struct spinlock lock;

	/** Determines whether the CPU is currently on. */
	bool is_on;

	/** Determine whether the CPU was already assigned to a VM. */
	bool is_assigned;
};

void cpu_module_init(const cpu_id_t *cpu_ids, size_t count);

size_t cpu_index(struct cpu *c);
struct cpu *cpu_find_index(size_t index);
bool cpu_on(struct cpu *c, ipaddr_t entry, uintreg_t arg);
void cpu_off(struct cpu *c);
struct cpu *cpu_find(cpu_id_t id);
struct cpu *cpu_get_next();
#endif
