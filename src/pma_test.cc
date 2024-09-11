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
 */

#include <gmock/gmock.h>

extern "C" {
#include "pg/pma.h"
#include "pg/dlog.h"
}

#include <sys/mman.h>   /* mmap */

#include <span>
#include <vector>

#include <stdlib.h>
#include <time.h>

#include "pma_test.hh"

namespace
{
using namespace ::std::placeholders;
using ::pma_test::get_ptable;

constexpr size_t TEST_HEAP_SIZE = MEMORY_SIZE;


/**
 * Get an STL representation of the page table.
 */
std::span<pte_t, MM_PTE_PER_PAGE> get_table(paddr_t pa)
{
	auto table = reinterpret_cast<struct mm_page_table *>(
		ptr_from_va(va_from_pa(pa)));
	return std::span<pte_t, MM_PTE_PER_PAGE>(table->entries,
						 std::end(table->entries));
}

class pma : public ::testing::Test
{
	void SetUp() override
	{
		printf("Setting up mm and pma (ignore Pointer outside of memory range errors)\n");

		test_heap = (uint8_t *) mmap((void*)(START_ADDRESS), TEST_HEAP_SIZE,
                    PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                   -1, 0);
		
        pages = pma_early_set_start_addr(0u);
		phys_start_address = (uintptr_t) pages;
		mpool_init(&ppool, sizeof(struct mm_page_table));
		mpool_add_chunk(&ppool, test_heap, TEST_HEAP_SIZE);
        mm_init(&ppool);
        mm_stage1_locked = mm_lock_stage1();
	}

	void TearDown() override
	{
		mm_unlock_stage1(&mm_stage1_locked);
	}
    
    protected:
		uint8_t *test_heap;
    	struct mpool ppool;
    	struct mm_stage1_locked mm_stage1_locked;
		pages_t *pages;
		uintptr_t phys_start_address;
};


/**
 * @brief TODO phys_start_address, look up definition
 */
TEST_F(pma, pma_get_fault_ptr)
{
	//EXPECT_EQ(PN_TO_PTR(phys_start_address), pma_get_fault_ptr());
	dlog("======================================== %0x", pma_get_fault_ptr());
	dlog(" Phys start address: %0x", phys_start_address);
}

/**
 * @brief Allocates physical memory pages and checks that the buffer is properly initialised.
 * Verifies that the pages surrounding the pointer to the starting address do not match the ID bit.
 * Tests that the allocated pages do. 
 * Tests that freeing the physical memory pages has no effect on the pages surrounding the starting pointer.
 * Further tests that the allocated pages do not match the ID bit anymore.
 */
TEST_F(pma, pma_alloc_free)
{
    char *buf;
	uint64_t buf_size = 10;
	uint8_t id = 3;
	pages_t id_bit = ID_TO_BIT(id);
	
	buf = (char *) pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), buf_size, MM_MODE_R, id, &ppool);

    EXPECT_EQ(buf[0], 0);

	uintptr_t start_pn_ptr = pma_get_start((uintptr_t) buf, id);

	uint64_t start_pn = PTR_TO_PN(start_pn_ptr);

	uint64_t end_pn = start_pn + BYTES_TO_PAGES(buf_size) - 1;

	/* Surrounding pages should not be assigned to "id" */
	EXPECT_NE(pages[start_pn - 1], id_bit);
	EXPECT_NE(pages[start_pn + 1], id_bit);

	/* But allocated pages need to be */
	for (int i = start_pn; i < end_pn; i++) {
		EXPECT_EQ(pages[i], id_bit);
	}

	pma_free(mm_stage1_locked.ptable, (uintptr_t) buf, id, &ppool);

	/* Check that freeing reverses this assignment, but surrounding pages stay the same */
	EXPECT_NE(pages[start_pn - 1], id_bit);
	EXPECT_NE(pages[start_pn + 1], id_bit);

	for (int i = start_pn; i < end_pn; i++) {
		EXPECT_NE(pages[i], id_bit);
	}
}

/**
 * @brief Allocates physical memory pages.
 * Checks that the corresponding buffer matches the start address of the provided pointer.
 */
TEST_F(pma, pma_get_start)
{
	char *buf;
	uint64_t buf_size = 10;
	uint8_t id = 3;
	// pages_t id_bit = ID_TO_BIT(id);
	
	buf = (char *) pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), buf_size, MM_MODE_R, id, &ppool);

	uintptr_t start_pn_ptr = pma_get_start((uintptr_t) &buf[5], id);

	EXPECT_EQ(buf, (void *) start_pn_ptr);
}

/**
 * @brief Allocates physical memory pages ten times. 
 * Checks that the reported size of each buffer matches the expected value based on the size of the allocated buffer.
 * If the buffer size exceeds the memory size or is equal to zero, then the reported size is expected to be zero.
 */
TEST_F(pma, pma_get_size)
{
	char *buf;
	
	uint8_t id = 3;

	srand( (unsigned)time(NULL) );
	uint64_t buf_size = 0;

	for (int i = 0; i < 10; i++) {
		buf = (char *) pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), buf_size, MM_MODE_R, id, &ppool);

		uint64_t reported_size = pma_get_size((uintptr_t) buf, id);
		
		if (buf_size > MEMORY_SIZE || buf_size == 0) {
			EXPECT_EQ(0, reported_size);
			buf_size = 0;
		} else {
			EXPECT_EQ((int(buf_size / 4096)) * 4096 + 4096, reported_size);
		}

		buf_size += rand() % 8192;
	}

	// faulty start memory location
	EXPECT_FALSE(pma_get_size(START_ADDRESS, 4));
}

/**
 * @brief Tests the assignment of physical memory allocation.
 */
TEST_F(pma, pma_assign)
{
	struct mm_ptable *hypervisor_ptable = nullptr;
	ipaddr_t ip_begin; 
	size_t size = 0x80000004;
	uint32_t mode = 16;
	uint8_t id = 8;
	uintptr_t ptr = pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), size, MM_MODE_R, id, &ppool);

	// invalid ID
	EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	id = 4;

	// memory assignment fails
	EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	size = 0x60000000;

	// IPA value provided to hypervisor for assignment
	EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, HYPERVISOR_ID, &ppool));

	// pointer exceeds page count
	EXPECT_FALSE(pma_assign(hypervisor_ptable, 524290, ip_begin, size, mode, id, &ppool));

	// assignment attempt in restricted section
	EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	ptr = PN_TO_PTR(*pages);

	// assignment to not allocated memory region
	EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	ptr = pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), size, MM_MODE_R, id, &ppool);

	// memory region already assigned to provided ID
	EXPECT_TRUE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));

	// TODO memory mapping fails
	//EXPECT_FALSE(pma_assign(hypervisor_ptable, ptr2, ip_begin, size, mode, id, &ppool));

	// valid assignments
	size = 0x40000000;
	mode = 8;
	EXPECT_TRUE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	size = 0x20000000;
	mode = 32;
	EXPECT_TRUE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
	size = 0x10000000;
	mode = 64;
	EXPECT_TRUE(pma_assign(hypervisor_ptable, ptr, ip_begin, size, mode, id, &ppool));
}

/**
 * @brief Tests the functionality of memory reservation.
 * 
 */
TEST_F(pma, pma_reserve_memory)
{
	uintptr_t begin = 0x8000000; 
	uintptr_t end = begin + 9;
	uint8_t id = 0;

	// pointer outside of memory range (case 1)
	EXPECT_FALSE(pma_reserve_memory(begin, end, id));
	
	// pointer outside of memory range (case 2)
	uint64_t page_aligned_address = (((uint64_t) 0x80000000) + (1 << 12) - 1);
	uint64_t num_pages_required = page_aligned_address / (1 << 12);
	begin = phys_start_address + num_pages_required * (1 << 12);
	EXPECT_FALSE(pma_reserve_memory(begin, end, id));

	// memory region too large
	begin = phys_start_address;
	end = PAGE_COUNT + 4;
	EXPECT_FALSE(pma_reserve_memory(begin, end, id));

	// page already reserved
    begin = phys_start_address - 16; 
	end = begin + 9;
	id = 0;
	EXPECT_FALSE(pma_reserve_memory(begin, end, id));
	// TODO for-loop case
	// EXPECT_FALSE(pma_reserve_memory(begin, end, id));

	// valid memory reservation
	begin = phys_start_address;
	end = begin + 17;
	EXPECT_TRUE(pma_reserve_memory(begin, end, id));
}

/**
 * @brief Tests the functionality of physical memory release.
 */
TEST_F(pma, pma_release_memory)
{
	// pointer outside of memory range
	uintptr_t begin = START_ADDRESS - 1;
	uintptr_t end = begin + 9;
	uint8_t id = 4;
	EXPECT_FALSE(pma_release_memory(begin, end, id));

    // pointer outside of memory range 
	EXPECT_FALSE(pma_release_memory(PN_TO_PTR(PAGE_COUNT + 1), end, id));

	// memory region too large
	EXPECT_FALSE(pma_release_memory(phys_start_address + 1, PN_TO_PTR(PAGE_COUNT + 2), id));

	// successful release of memory
	begin = pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), 8, MM_MODE_R, 4, &ppool);
	end = begin + 9;
	EXPECT_TRUE(pma_release_memory(begin, end, id));
	// TODO additional logging message
	EXPECT_TRUE(pma_release_memory(begin, end, id));
}

/**
 * @brief Allocates physical memory pages.
 * Tests that the region covered by the buffer with the provided size (offset) is assigned to the given thread.
 * Verifies that this is not the case for the region beyond.
 */
TEST_F(pma, pma_is_assigned)
{
	uint8_t id = 3;
	char *buf;
	uint64_t buf_size = 0x1000;

	buf = (char *) pma_alloc(mm_stage1_locked.ptable, ipa_init(PMA_IDENTITY_MAP), buf_size, MM_MODE_R, id, &ppool);

	EXPECT_TRUE(pma_is_assigned((uintptr_t) buf, buf_size, id));
	EXPECT_FALSE(pma_is_assigned((uintptr_t) &buf[buf_size + 1], 1, id));

	// TODO check test cases 500 504
	// start address is invalid
	//EXPECT_FALSE(pma_is_assigned(PN_TO_PTR(FAULT_PAGE_NUMBER), buf_size, id));

	// invalid ID
	id = MAX_IDS;
	EXPECT_FALSE(pma_is_assigned((uintptr_t) &buf[buf_size + 1], buf_size, id));

	// TODO success allocation of physical memory pages
	// id = 2;
	// EXPECT_TRUE(pma_is_assigned((uintptr_t) &buf[buf_size + 1], buf_size, id));
}

/**
 * @brief Tests the functionality of freeing physical memory.
 */
TEST_F(pma, pma_free) {
	struct mm_ptable *ptable = new mm_ptable;
	uintptr_t ptr = 0x80000000;
	uint8_t id = MAX_IDS;

	// invalid ID
	EXPECT_FALSE(pma_free(ptable, ptr, id, &ppool));

	id--;
	ptr = PN_TO_PTR(PAGE_COUNT);
	// TODO if (pages[start_pn] == 0) 
	//EXPECT_FALSE(pma_free(ptable, ptr, id, &ppool));
}

} /* namespace */

namespace pma_test
{
/**
 * Get an STL representation of the ptable.
 */
std::vector<std::span<pte_t, MM_PTE_PER_PAGE>> get_ptable(
	const struct mm_ptable &ptable)
{
	std::vector<std::span<pte_t, MM_PTE_PER_PAGE>> all;
	const uint8_t root_table_count = arch_mm_stage2_root_table_count();
	for (uint8_t i = 0; i < root_table_count; ++i) {
		all.push_back(get_table(
			pa_add(ptable.root, i * sizeof(struct mm_page_table))));
	}
	return all;
}

} /* namespace pma_test */
