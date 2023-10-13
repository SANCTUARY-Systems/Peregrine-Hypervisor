/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <stdbool.h>

#include "pg/arch/cpu.h"

#include "pg/fdt.h"
#include "pg/mm.h"
#include "pg/mpool.h"

#define MAX_MEM_RANGES 20
#define MAX_DEVICE_MEM_RANGES 10

struct mem_range {
	paddr_t begin;
	paddr_t end;
};

struct boot_params {
	cpu_id_t cpu_ids[MAX_CPUS];
	size_t cpu_count;
	struct mem_range mem_ranges[MAX_MEM_RANGES];
	size_t mem_ranges_count;
	struct mem_range device_mem_ranges[MAX_DEVICE_MEM_RANGES];
	size_t device_mem_ranges_count;
	paddr_t initrd_begin;
	paddr_t initrd_end;
	uintreg_t kernel_arg;
};

struct boot_params_update {
	struct mem_range reserved_ranges[MAX_MEM_RANGES];
	size_t reserved_ranges_count;
	paddr_t initrd_begin;
	paddr_t initrd_end;
};
