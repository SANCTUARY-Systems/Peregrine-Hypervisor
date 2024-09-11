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

#include "pg/cpu.h"

#include <stdalign.h>

#include "pg/arch/cache.h"

#include "pg/api.h"
#include "pg/check.h"
#include "pg/dlog.h"

#include "vmapi/pg/call.h"

/**
 * The stacks to be used by the CPUs.
 *
 * Align to page boundaries to ensure that cache lines are not shared between a
 * CPU's stack and data that can be accessed from other CPUs. If this did
 * happen, there may be coherency problems when the stack is being used before
 * caching is enabled.
 */
alignas(PAGE_SIZE) static char callstacks[MAX_CPUS][STACK_SIZE]; //TODO: add a guard page at the end of the stack(s) to detect and prevent overflows

/* NOLINTNEXTLINE(misc-redundant-expression) */
static_assert((STACK_SIZE % PAGE_SIZE) == 0, "Keep each stack page aligned.");
static_assert((PAGE_SIZE % STACK_ALIGN) == 0,
	      "Page alignment is too weak for the stack.");

/* State of all supported CPUs. The stack of the first one is initialized. */
struct cpu cpus[MAX_CPUS] = {
	{
		.is_on = 1,
		.stack_bottom = &callstacks[0][STACK_SIZE],
	},
};

uint32_t cpu_count = 1;

void cpu_module_init(const cpu_id_t *cpu_ids, size_t count)
{
	uint32_t i;
	cpu_id_t boot_cpu_id = cpus[0].id;
	bool found_boot_cpu = false;

	cpu_count = count;

	/*
	 * Initialize CPUs with the IDs from the configuration passed in. The
	 * CPUs after the boot CPU are initialized in reverse order. The boot
	 * CPU is initialized when it is found or in place of the last CPU if it
	 * is not found.
	 */
	/*
	 * We don't initialize the CPUs in reverse order to keep the cores of
	 * on one cluster together
	 */
	for (i = 0; i < cpu_count; ++i) {
		struct cpu *c;
		cpu_id_t id = cpu_ids[i];

		if (found_boot_cpu || id != boot_cpu_id) {
			c = &cpus[i];
			c->stack_bottom = &callstacks[i][STACK_SIZE];
			//--j;
			//c = &cpus[j];
			//c->stack_bottom = &callstacks[j][STACK_SIZE];
		} else {
			found_boot_cpu = true;
			c = &cpus[0];
			CHECK(c->stack_bottom == &callstacks[0][STACK_SIZE]);
		}

		sl_init(&c->lock);
		c->id = id;
		/* Mark CPUs as unassigned */
		c->is_assigned = false;
	}

	if (!found_boot_cpu) {
		/* Boot CPU was initialized but with wrong ID. */
		dlog_warning("Boot CPU's ID not found in config.\n");
		cpus[0].id = boot_cpu_id;
	}

	/*
	 * Clean the cache for the cpus array such that secondary cores
	 * hitting the entry point can read the cpus array consistently
	 * with MMU off (hence data cache off).
	 */
	arch_cache_data_clean_range(va_from_ptr(cpus), sizeof(cpus));

	arch_cache_data_clean_range(va_from_ptr(&cpu_count), sizeof(cpu_count));
}

size_t cpu_index(struct cpu *c)
{
	return c - cpus;
}

/*
 * Return cpu with the given index.
 */
struct cpu *cpu_find_index(size_t index)
{
	return (index < MAX_CPUS) ? &cpus[index] : NULL;
}

/**
 * Turns CPU on and returns the previous state.
 */
bool cpu_on(struct cpu *c, ipaddr_t entry, uintreg_t arg)
{
	bool prev;
	uint16_t cpu_index_local;

	sl_lock(&c->lock);
	prev = c->is_on;
	c->is_on = true;
	sl_unlock(&c->lock);

	if (!prev) {
		/* This returns the first booted VM (e.g. primary in the NWd) */
		//struct vm *vm = vm_get_first_boot();
		//struct vcpu *vcpu = vm_get_vcpu(vm, cpu_index(c));

		struct vm *vm = vm_find_from_cpu(c);
        cpu_index_local = vm_local_cpu_index(c);
        RET(cpu_index_local == (uint16_t) -1, prev,
            "Unable to identify vCPU index of CPU %#x\n", c->id);

		struct vcpu *vcpu = vm_get_vcpu(vm, cpu_index_local);
		struct vcpu_locked vcpu_locked;

		vcpu_locked = vcpu_lock(vcpu);
		vcpu_on(vcpu_locked, entry, arg);
		vcpu_unlock(&vcpu_locked);
	}

	return prev;
}

/**
 * Prepares the CPU for turning itself off.
 */
void cpu_off(struct cpu *c)
{
	sl_lock(&c->lock);
	c->is_on = false;
	sl_unlock(&c->lock);
}

/**
 * Searches for a CPU based on its ID.
 */
struct cpu *cpu_find(cpu_id_t id)
{
	size_t i;
	uint32_t cpu_max = cpu_count > MAX_CPUS ? MAX_CPUS : cpu_count; // play it safe - makes static analysis happier

	for (i = 0; i < cpu_max; i++) {
		if (cpus[i].id == id) {
			return &cpus[i];
		}
	}

	return NULL;
}

/**
 * Get next unassigned CPU
 */
struct cpu *cpu_get_next()
{
	size_t i;

	for (i = 0; i < MAX_CPUS; i++) {
		if (!(cpus[i].is_assigned)) {
			cpus[i].is_assigned = true;
			return &cpus[i];
		}
	}
	return NULL;
}




