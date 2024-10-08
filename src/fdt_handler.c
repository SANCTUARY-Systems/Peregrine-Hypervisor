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

#include "pg/fdt_handler.h"

#include "pg/check.h"
#include "pg/cpu.h"
#include "pg/dlog.h"
#include "pg/fdt.h"
#include "pg/mm.h"
#include "pg/std.h"
#include "pg/pma.h"

/**
 * Initializes the FDT struct with the pointer to the FDT data (header) in
 * fdt_ptr.
 */
bool fdt_struct_from_ptr(const void *fdt_ptr, struct fdt *fdt)
{
	size_t fdt_size;

	if (!fdt_ptr || !fdt) {
		return false;
	}

	return fdt_size_from_header(fdt_ptr, &fdt_size) &&
	       fdt_init_from_ptr(fdt, fdt_ptr, fdt_size);
}

/**
 * Finds the memory region where initrd is stored.
 */
bool fdt_find_initrd(const struct fdt *fdt, paddr_t *begin, paddr_t *end)
{
	struct fdt_node n;
	uint64_t initrd_begin;
	uint64_t initrd_end;

	if (!fdt_find_node(fdt, "/chosen", &n)) {
		dlog_error("Unable to find '/chosen'\n");
		return false;
	}

	if (!fdt_read_number(&n, FDT_PROP_INITRD_START, &initrd_begin)) {
		dlog_error("Unable to read " FDT_PROP_INITRD_START "\n");
		return false;
	}

	if (!fdt_read_number(&n, FDT_PROP_INITRD_END, &initrd_end)) {
		dlog_error("Unable to read " FDT_PROP_INITRD_END "\n");
		return false;
	}

	*begin = pa_init(initrd_begin);
	*end = pa_init(initrd_end);

	return true;
}

bool fdt_find_cpus(const struct fdt *fdt, cpu_id_t *cpu_ids, size_t *cpu_count)
{
	static const struct string str_cpu = STRING_INIT("cpu");
	struct fdt_node n;
	size_t addr_size;

	*cpu_count = 0;

	if (!fdt_find_node(fdt, "/cpus", &n)) {
		dlog_error("Unable to find '/cpus'\n");
		return false;
	}

	if (!fdt_address_size(&n, &addr_size)) {
		return false;
	}

	if (!fdt_first_child(&n)) {
		return false;
	}

	do {
		struct memiter data;

		if (!fdt_read_property(&n, "device_type", &data) ||
		    !string_eq(&str_cpu, &data) ||
		    !fdt_read_property(&n, "reg", &data)) {
			continue;
		}

		/* Get all entries for this CPU. */
		while (memiter_size(&data)) {
			uint64_t value;

			if (*cpu_count >= MAX_CPUS) {
				dlog_error("Found more than %d CPUs\n",
					   MAX_CPUS);
				return false;
			}

			if (!fdt_parse_number(&data, addr_size, &value)) {
				dlog_error("Could not parse CPU id\n");
				return false;
			}
			cpu_ids[(*cpu_count)++] = value;
		}
	} while (fdt_next_sibling(&n));

	return true;
}

bool fdt_find_memory_ranges(const struct fdt *fdt,
			    const struct string *device_type,
			    struct mem_range *mem_ranges,
			    size_t *mem_ranges_count, size_t mem_range_limit)
{
	struct fdt_node n;
	size_t addr_size;
	size_t size_size;
	size_t mem_range_index = 0;

	if (!fdt_find_node(fdt, "/", &n) || !fdt_address_size(&n, &addr_size) ||
	    !fdt_size_size(&n, &size_size)) {
		return false;
	}

	/* Look for nodes with the device_type set to `device_type`. */
	if (!fdt_first_child(&n)) {
		return false;
	}

	do {
		struct memiter data;

		if (!fdt_read_property(&n, "device_type", &data) ||
		    !string_eq(device_type, &data) ||
		    !fdt_read_property(&n, "reg", &data)) {
			continue;
		}

		/* Traverse all memory ranges within this node. */
		while (memiter_size(&data)) {
			uintpaddr_t addr;
			size_t len;

			CHECK(fdt_parse_number(&data, addr_size, &addr));
			CHECK(fdt_parse_number(&data, size_size, &len));

			if (mem_range_index < mem_range_limit) {
				mem_ranges[mem_range_index].begin =
					pa_init(addr);
				mem_ranges[mem_range_index].end =
					pa_init(addr + len);
				++mem_range_index;
			} else {
				dlog_error(
					"Found %s range %u in FDT but only %u "
					"supported, ignoring additional range "
					"of size %u.\n",
					string_data(device_type),
					mem_range_index, mem_range_limit, len);
			}
		}
	} while (fdt_next_sibling(&n));
	*mem_ranges_count = mem_range_index;

	return true;
}

bool fdt_map(struct fdt *fdt, struct mm_stage1_locked stage1_locked,
	     paddr_t fdt_addr, struct mpool *ppool)
{
	const void *fdt_ptr;
	size_t fdt_len;

	/* Map the fdt header in. */
	fdt_ptr = mm_identity_map_and_reserve(stage1_locked, fdt_addr,
				  pa_add(fdt_addr, FDT_V17_HEADER_SIZE+1),
				  MM_MODE_R, ppool);
	if (!fdt_ptr) {
		dlog_error("Unable to map FDT header.\n");
		return NULL;
	}

	if (!fdt_size_from_header(fdt_ptr, &fdt_len)) {
		dlog_error("FDT failed header validation.\n");
		goto fail;
	}

	/* Map the rest of the fdt in, but only if its not already mapped  */
	if(fdt_len  > PAGE_SIZE)
	{
		pma_release_memory(fdt_addr.pa, (pa_add(fdt_addr, FDT_V17_HEADER_SIZE)).pa, HYPERVISOR_ID);
		fdt_ptr = mm_identity_map_and_reserve(stage1_locked, fdt_addr,
				  pa_add(fdt_addr, fdt_len), MM_MODE_R, ppool);
	}
	if (!fdt_ptr) {
		dlog_error("Unable to map full FDT.\n");
		goto fail;
	}

	if (!fdt_init_from_ptr(fdt, fdt_ptr, fdt_len)) {
		dlog_error("FDT failed validation.\n");
		goto fail_full;
	}

	return true;

fail_full:
	mm_unmap(stage1_locked, fdt_addr, pa_add(fdt_addr, fdt_len), ppool);
	return false;

fail:
	mm_unmap(stage1_locked, fdt_addr, pa_add(fdt_addr, FDT_V17_HEADER_SIZE),
		 ppool);
	return false;
}

bool fdt_unmap(struct fdt *fdt, struct mm_stage1_locked stage1_locked,
	       struct mpool *ppool)
{
	paddr_t begin = pa_from_va(va_from_ptr(fdt_base(fdt)));
	paddr_t end = pa_add(begin, fdt_size(fdt));

	if (!mm_unmap(stage1_locked, begin, end, ppool)) {
		return false;
	}

	/* Invalidate pointer to the buffer. */
	fdt_fini(fdt);
	return true;
}

/**
 * Gets the size of the first memory range from the FDT into size.
 *
 * The test framework expects the address space to be contiguous, therefore
 * gets the size of the first memory range, if there is more than one range.
 */
bool fdt_get_memory_size(const struct fdt *fdt, size_t *size)
{
	const struct string memory_device_type = STRING_INIT("memory");
	struct mem_range mem_range;
	size_t mem_ranges_count;

	if (!fdt || !size ||
	    !fdt_find_memory_ranges(fdt, &memory_device_type, &mem_range,
				    &mem_ranges_count, 1)) {
		return false;
	}

	if (mem_ranges_count < 1) {
		return false;
	}

	*size = pa_difference(mem_range.begin, mem_range.end);
	dlog_debug("memory size from FDT: %#x - %#x\n", mem_range.begin, mem_range.end);

	return true;
}
