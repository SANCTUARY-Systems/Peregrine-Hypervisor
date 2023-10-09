/*
 * Copyright 2021 Sanctuary
 */

#include "../inc/pg/pma.h"

#include "pg/dlog.h"
#include "pg/layout.h"
#include "pg/spinlock.h"
#include "pg/std.h"

/*
 * Memory allocator working a page granularity. It traces the use of physical
 * memory pages. A page can be assigned to multiple entities (IDs). During
 * allocation an IDs is specified to which the allocated pages belong.
 * Afterwards more IDs can be assigned and also IDs absigned. To free an memory
 * allocation it must be assigned to only on ID (i.e., calling absign() for all
 * other IDs before).
 *
 * Memory allocations can span multiple pages, multiple pages belonging to the
 * same allocation memory region are continuous, the end of an allocation region
 * is marked as the status of the last page (LAST_PAGE is set).
 *
 * The page FAULT_PAGE_NUMBER is always allocated and is mapped inaccessible.
 */

// TODO: use locks to prevent concurrent manipulations of the pages array

uintptr_t phys_start_address = START_ADDRESS;

uintptr_t pma_get_fault_ptr()
{
	return PN_TO_PTR(FAULT_PAGE_NUMBER);
}

#if defined HOST_TESTING_MODE && HOST_TESTING_MODE != 0
#include <malloc.h>
#include <stdlib.h>
pages_t *pages;

// #define PHYS_START_ADDRESS TEST_PHYS_START_ADDRESS

pages_t *pma_early_set_start_addr(uintptr_t start_addr)
{
	pages = (pages_t *)(((unsigned long)malloc(PAGE_COUNT *
						   sizeof(pages_t)) +
			     4095) &
			    ~(size_t)0xFFF);  // 4k align
	phys_start_address = (uintptr_t)pages;
	return pages;
}
#else
pages_t *pages;
#endif

struct spinlock pages_spinlock;

static struct mpool *hypervisor_ppool;
static struct mm_ptable *hypervisor_ptable;

struct alloc_cache_entry {
	uintptr_t addr;
	uint64_t begin;
	uint64_t end;
	size_t page_count;
	pages_t owner_ids;  // one hot
};

#if !defined(DISABLE_PAGE_ALLOC_CACHE)

// This macro generates numbers from 0 to 31; used to index cache
#define PTR_HASH(x) ((size_t)((uintptr_t)(((x) & 0x0FFFFFFF) >> 12) % 0x1F))

static struct alloc_cache_entry alloc_cache[32];

static inline struct alloc_cache_entry *get_cached(uintptr_t addr)
{
	if (alloc_cache[PTR_HASH(addr)].addr == addr) {
		return &alloc_cache[PTR_HASH(addr)];
	}

	return NULL;
}

static inline void add_alloc_cache(uintptr_t addr, uint64_t begin, uint64_t end,
				   size_t page_count, uint8_t owner)
{
	struct alloc_cache_entry *entry = &alloc_cache[PTR_HASH(addr)];
	entry->addr = addr;
	entry->begin = begin;
	entry->end = end;
	entry->page_count = page_count;
	entry->owner_ids = ID_TO_BIT(owner);
}
#else
static inline struct alloc_cache_entry *get_cached(uintptr_t addr)
{
	return NULL;
}
static inline void add_alloc_cache(uintptr_t addr, uint64_t begin, uint64_t end,
				   size_t page_count, uint8_t owner)
{
}
#endif

#if LOG_LEVEL == LOG_LEVEL_VERBOSE
/* Stuff below is used to accelerate pma_print_chunks in debug builds */
pages_t used_ids;

static inline void add_ids_used(uint8_t id)
{
	used_ids |= ID_TO_BIT(id);
}
static inline void rem_ids_used(uint8_t id)
{
	used_ids &= ~ID_TO_BIT(id);
}
static inline int get_id_count()
{
	return __builtin_popcount(used_ids);
}

#else
static inline void add_ids_used(__attribute__((unused)) uint8_t id)
{
}
static inline void rem_ids_used(__attribute__((unused)) uint8_t id)
{
}
#endif

// void print_bits(uint8_t num)
//{
//   for(int bit = (sizeof(num) * 8) - 1; bit >= 0; bit--)
//   {
//      dlog_debug("%i", (num >> bit) & 0x01);
//   }
//}
// void print_allocations(uint64_t num)
//{
//	dlog_debug("-------------------\n");
//	for(uint64_t i = 0; i < num; i++)
//	{
//		dlog_debug("pages[%3lu]:", i);
//		print_bits(pages[i]);
//		dlog_debug("\n");
//	}
//}

// check if ipa_begin is aligned according to the given alignment
// and if not calculate the offset for an alloc request such that the alignment 
// can be achieved without using too many page tables
static inline uint64_t pma_calc_ipa_offset(ipaddr_t ipa_begin, uint64_t alignment)
{
	uint64_t align_zeroes_mask = (1ull << (alignment + PAGE_BITS)) - 1;
	uint64_t align_offset = 0;
	if ((ipa_addr(ipa_begin) == PMA_IDENTITY_MAP) || (ipa_addr(ipa_begin) & align_zeroes_mask) == 0)
	{
		// no offset required
		return align_offset;
	}
	// calc the number of pages the ipa is offset from the alignment, to get the same offset
	// for the physical starting page number
	align_offset = (ipa_addr(ipa_begin) & align_zeroes_mask) >> PAGE_BITS;	

	return align_offset;
}

/**
 * @brief Prints the allocated chunks. Only prints at the verbose log level.
 * 
 */
void pma_print_chunks()
{
#if LOG_LEVEL < LOG_LEVEL_VERBOSE
	return;
#else
	uint8_t id;
	uint64_t i;
	uintptr_t begin = 0;
	uintptr_t end = 0;
	bool in_chunk;
	struct alloc_cache_entry *entry;

	uint8_t max_id = MAX_IDS;

	max_id = get_id_count();

	for (id = 0; id < max_id; id++) {
		in_chunk = false;
		for (i = 0; i < PAGE_COUNT; i++) {
			if (!in_chunk && (pages[i] & ID_TO_BIT(id))) {
				begin = PN_TO_PTR(i);
				in_chunk = true;

				entry = get_cached(begin);
				if (entry && entry->owner_ids & ID_TO_BIT(id)) {
					in_chunk = false;
					dlog_verbose(
						"PMA allocation %#x - %#x (id: "
						"%d)\n",
						begin,
						PN_TO_PTR(entry->end) +
							PAGE_SIZE - 1,
						id);
					i = entry->end + 1;
					continue;
				}
			}
			if (in_chunk && (pages[i] & ID_TO_LAST_PAGE_BIT(id))) {
				end = PN_TO_PTR(i);
				in_chunk = false;
				dlog_verbose("PMA allocation %#x - %#x (id: %d)\n",
					  begin, end + PAGE_SIZE - 1, id);
			}
		}
	}
#endif
}

// check if a page number (pn) belongs to a restricted memory section
// that should never be re-assigned, freed, etc. like the FAULT_PAGE_NUMBER
// or the pages array
static bool is_restricted(uint64_t pn)
{
	// TODO: find a better, more scalable and manageable solution
	return pn == FAULT_PAGE_NUMBER 
	    || (pn >= PTR_TO_PN(pages) 
		&& pn < PTR_TO_PN(&(pages[PAGE_COUNT])));
}

// check if the provided id is valid, e.g., that is is not too large
bool is_valid_id(uint8_t id)
{
	// check whether a valid ID is provided (two bit per ID)
	if (id >= MAX_IDS) {
		dlog_error("Illegal ID: 0x%02X\n", id);
		return false;
	}
	return true;
}

// check whether the provided page number (pn) is the first page of an
// allocation chunk
bool is_start_page(uint64_t pn, uint8_t id)
{
	if (pn >= PAGE_COUNT) {
		return false;
	}

	// to be a the first page of an allocation chunk (start_page),
	// either pn has to be the first page (page zero)
	// or the previous page is not allocated
	// or the previous page is the last page of the previous allocation
	// chunk
	return (pn == 0 || (pages[pn - 1] == 0 || (pages[pn - 1] & ID_TO_LAST_PAGE_BIT(id))));
}

// finds the number of the start page, i.e. first page, of a memory chunk,
// corresponding to a given pointer
uint64_t get_start_page_number(uintptr_t ptr, uint8_t id)
{
	struct alloc_cache_entry *entry = get_cached(ptr);  
	// by EXPERIENCE, this is the most likely case anyway :)

	if (entry) {
		return entry->begin;
	}

	uint64_t pn = PTR_TO_PN(ptr);	 
	// TODO: calculate page-number from pointer and
	// check whether the pointer is page alligned

	if (pn >= PAGE_COUNT) {
#if !defined(HOST_TESTING_MODE) || HOST_TESTING_MODE == 0
		dlog_error("Pointer (ptr: %p) outside of memory range\n", ptr);
#endif
		return FAULT_PAGE_NUMBER;
	}

	// check if page is allocated, if not return an error
	if (pages[pn] == 0) {
		dlog_error("Pointer to unallocated memory provided (ptr: %p)\n", ptr);
		return FAULT_PAGE_NUMBER;
	}

	// go through the pages array backwards until the beginning of the
	// allocation is found
	uint64_t start_pn = pn;
	while (start_pn > 0 &&
	       !((pages[start_pn - 1] & ID_TO_BIT(id)) == 0 ||
		 (pages[start_pn - 1] & ID_TO_LAST_PAGE_BIT(id)))) {
		start_pn -= 1;
	}
	return start_pn;
}

//map memory using Peregrine's identity_map functions
// ipa_begin is unused for hypervisor mapping
uintptr_t map_memory(struct mm_ptable* p, ipaddr_t ipa_begin, uint64_t start_pn, uint64_t end_pn, uint32_t mode, uint8_t id, struct mpool *ppool)
{
	if(id == HYPERVISOR_ID)
	{
		// dlog_debug("pma_identity_map: %#x - %#x\n", PN_TO_PTR(start_pn), pa_init(PN_TO_PTR(end_pn+1)));
		if(mm_identity_map_ptable(*p, pa_init(PN_TO_PTR(start_pn)), pa_init(PN_TO_PTR(end_pn+1)), mode, ppool) != (void*)PN_TO_PTR(start_pn))
		{
			return PN_TO_PTR(FAULT_PAGE_NUMBER);
		}
	}
	else
	{
		// dlog_debug("pma_identity_map: %#x - %#x | IPA begin: %#x\n", PN_TO_PTR(start_pn), pa_init(PN_TO_PTR(end_pn+1)), ipa_addr(ipa_begin));
	
		if (!mm_vm_prepare(p, ipa_begin, pa_init(PN_TO_PTR(start_pn)), pa_init(PN_TO_PTR(end_pn+1)), mode, ppool))
		{
			return PN_TO_PTR(FAULT_PAGE_NUMBER);
		}

		mm_vm_commit(p, ipa_begin, pa_init(PN_TO_PTR(start_pn)), pa_init(PN_TO_PTR(end_pn+1)), mode, ppool, NULL); //FIXME: ipa set to NULL
	}

	return (uintptr_t)PN_TO_PTR(start_pn);
}

uintptr_t unmap_memory(struct mm_ptable *p, uint64_t start_pn, uint64_t end_pn,
		       uint8_t id, struct mpool *ppool)
{
	return map_memory(p, ipa_init(PN_TO_PTR(start_pn)), start_pn, end_pn, MM_MODE_UNMAPPED_MASK, id, ppool);
}

// get the size of the memory allocation
size_t pma_get_size(uintptr_t ptr, uint8_t id)
{
	struct alloc_cache_entry *entry = get_cached(ptr);

	if (entry) {
		return PAGES_TO_BYTES(entry->page_count);
	}

	uint64_t start_pn = get_start_page_number(ptr, id);

	if (start_pn == FAULT_PAGE_NUMBER) {
		return 0;
	}

	uint64_t size = 0;
	for (uint64_t i = start_pn; i < PAGE_COUNT; i++) {
		size += 1;
		if (pages[i] & ID_TO_LAST_PAGE_BIT(id)) {
			break;
		}
	}

	return PAGES_TO_BYTES(size);
}

/**
 * @brief Returns the start address of the provided pointer's memory region.
 * 
 * @param ptr pointer
 * @param id 
 * @return uintptr_t 
 */
uintptr_t pma_get_start(uintptr_t ptr, uint8_t id)
{
	uint64_t start_pn = get_start_page_number(ptr, id);

	// Explicit check not necessary:
	//	if(start_pn == FAULT_PAGE_NUMBER)
	//	{
	//		return PN_TO_PTR(FAULT_PAGE_NUMBER);
	//	}

	return PN_TO_PTR(start_pn);
}

// make reserved memory, this function should only be used during
// initialization, i.e., to mark memory that is implicitly assigned
bool pma_reserve_memory(uintptr_t begin, uintptr_t end, uint8_t id)
{
	uint64_t start_pn = PTR_TO_PN(begin);
	uint64_t end_pn = PTR_TO_PN(end-1); //end address is the first that does NOT belong to the memory region

	if (begin < phys_start_address || start_pn >= PAGE_COUNT) {
		#if !defined(HOST_TESTING_MODE) || HOST_TESTING_MODE == 0
		if (begin < phys_start_address) dlog_error("Pointer %p outside of memory range: %p < %p\n", begin, begin, phys_start_address);
		else dlog_error("Pointer %p outside of memory range: %p >= %u\n", begin, start_pn, PAGE_COUNT);
		#endif
		return false;
	}

	if (end_pn > PAGE_COUNT) {
		dlog_error("Memory region too large (%u)\n", end - begin);
		return false;
	}

	for (uint64_t i = start_pn; i <= end_pn; i++) {
		if (pages[i] == 0) {
			pages[i] = ID_TO_BIT(id);
			if (i == end_pn) {
				sl_lock(&pages_spinlock);
				pages[i] |= ID_TO_LAST_PAGE_BIT(id);
				sl_unlock(&pages_spinlock);
			}
		} else {
			// if an already reserved page is encountered, reset all previous pages to zero and return
			dlog_error("Already reserved page encountered.\n");

			// memset_unsafe(&pages[start_pn], 0, (i -
			// 1u)*sizeof(pages_t)); // FIXME crashes for some
			// reason... Not the over invocations though
			sl_lock(&pages_spinlock);
			for (uint64_t j = i - 1; j >= start_pn; j--) {
				pages[j] = 0;
			}
			sl_unlock(&pages_spinlock);
			return false;
		}
	}
	return true;
}

// make reserved memory as no longer reserved,
// this function should only be used during initialization,
bool pma_release_memory(uintptr_t begin, uintptr_t end, uint8_t id)
{
	uint64_t start_pn = PTR_TO_PN(begin);
	// end address is the first which does not belong to the memory region
	uint64_t end_pn = PTR_TO_PN(end - 1);  

	if (begin < phys_start_address) {
		dlog_error("Pointer outside of memory range: %p < %p\n", begin, phys_start_address);
		return false;
	} 
	
	if (start_pn >= PAGE_COUNT) {
		dlog_error("Pointer outside of memory range: %p >= %u\n", start_pn, PAGE_COUNT);
		return false;
	} 

	if (end_pn > PAGE_COUNT) {
		dlog_error("Memory region too large (%u)\n", end - begin);
		return false;
	}

	if (!(pages[end_pn] & ID_TO_LAST_PAGE_BIT(id))) {
		dlog_error(
			"Releasing partial memory (%#x - %#x) leading to "
			"potential inconsistent memory allocation state.\n",
			begin, end);
	}

	// set the last page before the memory to be freed as the end
	// of a memory chunk (if the previous page exists and is not zero)
	if (start_pn > 0 && (pages[start_pn - 1] & ID_TO_BIT(id)) != 0) {
		sl_lock(&pages_spinlock);
		pages[start_pn - 1] = pages[start_pn - 1] | ID_TO_LAST_PAGE_BIT(id);
		sl_unlock(&pages_spinlock);
	}

	for (uint64_t i = start_pn; i <= end_pn; i++) {
		if ((pages[i] & ID_TO_BIT(id)) != 0) {
			sl_lock(&pages_spinlock);
			pages[i] = pages[i] & (~ID_TO_BIT(id));
			sl_unlock(&pages_spinlock);
			if (pages[i] & ID_TO_LAST_PAGE_BIT(id)) {
				pages[i] = pages[i] & (~ID_TO_LAST_PAGE_BIT(id));
				if (i != end_pn) {
					dlog_error(
						"Reached end of memory chunk "
						"while releasing memory.\n");
				}
			}
		}
	}

	return true;
}

/**
 * @brief Checks whether a region is assigned to the provided ID.
 * 
 * @param ptr indicator of the start of the memory region
 * @param size 
 * @param id 
 * @return true 
 * @return false 
 */
bool pma_is_assigned(uintptr_t ptr, size_t size, uint8_t id)
{
	struct alloc_cache_entry *entry = get_cached(ptr);

	if (entry) {
		return (entry->owner_ids & ID_TO_BIT(id));
	}

	uint64_t start_pn = PTR_TO_PN(ptr);
	uint64_t end_pn = PTR_TO_PN(ptr + size - 1);

	if (start_pn == FAULT_PAGE_NUMBER || !is_valid_id(id)) {
		return false;
	}

	pages_t id_bit = ID_TO_BIT(id);

	while (start_pn <= end_pn) {
		if (!(pages[start_pn] & id_bit)) {
			return false;
		}
		start_pn++;
	}

	return true;
}

bool pma_init(struct mm_stage1_locked stage1_locked, struct mpool *ppool)
{
	bool result = true;
	sl_init(&pages_spinlock);
#if !defined HOST_TESTING_MODE || HOST_TESTING_MODE == 0
	dlog_debug("pma_init map %#x - %#x\n", layout_data_end(), layout_data_end(), sizeof(pages_t) + PAGE_COUNT);
	pages = mm_identity_map(
		stage1_locked, layout_data_end(),
		pa_add(layout_data_end(), sizeof(pages_t) * PAGE_COUNT),
		MM_MODE_R | MM_MODE_W, ppool);
#else
	pma_early_set_start_addr(0x0);
#endif
	memset_unsafe(pages, 0,
		      PAGE_COUNT * sizeof(pages_t));  // set allocation status
						      // of all pages to zero
	// mark those pages holding the allocation information as allocated.

	result = pma_reserve_memory((uintptr_t)pages,
				    (uintptr_t)(pages + (PAGE_COUNT + 1)),
				    HYPERVISOR_ID);

	// if pages is part of peregrine's data segment, which gets reserved in
	// the the function mm_init, nothing is to do here...

	pages[FAULT_PAGE_NUMBER] =
		ID_TO_LAST_PAGE_BIT(HYPERVISOR_ID) |
		ID_TO_BIT(HYPERVISOR_ID);  // reserve the first page for the
					   // hypervisor

	// TODO: update memory mapping the map FAULT_PAGE_NUMBER (typically the
	// first page) as inaccessible (access to NULL should trigger a fault)
	mm_identity_map(
		stage1_locked, pa_init(PN_TO_PTR(FAULT_PAGE_NUMBER)),
		pa_add(pa_init(PN_TO_PTR(FAULT_PAGE_NUMBER)), PAGE_SIZE),
		MM_MODE_I, ppool);

	hypervisor_ppool = ppool;
	hypervisor_ptable = stage1_locked.ptable;

	// reserve memory that is mapped by A-TF (EL3)
	// TODO: define reserved memory dynamically, don't use hard coded values
	// pma_reserve_memory(0x82000000, 0x82008000, HYPERVISOR_ID);

	return result;
}
/*
 * This function allocates physical memory pages, when a 1-to-1 mapping is desired, set ipa_begin = ipa_init(PMA_IDENTITY_MAP)
 */
uintptr_t pma_alloc(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint32_t mode, uint8_t id, struct mpool *ppool)
{
	return pma_aligned_alloc(p, ipa_begin, size, 0, mode, id, ppool);
}

void pma_update_pool(struct mpool *ppool)
{
	hypervisor_ppool = ppool;
}

//allocate (continuous) physical memory pages for requested amount of memory (if possible)
//the allocated memory is accounted to the provided ID
//when a 1-to-1 mapping is desired as for the hypervisor, set ipa_begin = ipa_init(PMA_IDENTITY_MAP)
uintptr_t pma_aligned_alloc(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint8_t alignment, uint32_t mode, uint8_t id, struct mpool *ppool)
{
	uintptr_t ret_val;
	uint64_t align_offset;

	if (size <= 0) {
		dlog_error("Size of allocation is zero or smaller.\n");
		return PN_TO_PTR(FAULT_PAGE_NUMBER);
	}

	if (size > MEMORY_SIZE) {
		dlog_error(
			"Requested memory chunk (%u) larger than total memory "
			"(%u)!\n",
			size, MEMORY_SIZE);
		return PN_TO_PTR(FAULT_PAGE_NUMBER);
	}

	if (!is_valid_id(id)) {
		return PN_TO_PTR(FAULT_PAGE_NUMBER);
	}	

	// align to the largest page level smaller than the size
	// For this we must also keep the offset in mind, as the total
	// size required to map a larger page must be page size plus the
	// size required to get to the start of a large page
	if(alignment == PMA_ALIGN_AUTO_PAGE_LVL)
	{
		alignment = 0;
		for (uint8_t lvl = 1; lvl <= arch_mm_stage2_max_level(); lvl++)
		{
			alignment = lvl * PAGE_LEVEL_BITS;
			align_offset = pma_calc_ipa_offset(ipa_begin,alignment);
			if((size < mm_entry_size(lvl)) ||
			(align_offset > 0 && size < (mm_entry_size(lvl) + PAGE_SIZE*((1 << alignment) - align_offset))))
			{
				alignment = (lvl-1) * PAGE_LEVEL_BITS;
				break;
			}
		}
		
	}

	pages_t id_bit = ID_TO_BIT(id);

	uint64_t start_pn = 0;
	uint64_t page_count = 0;
	uint64_t align_incr = 0;
	align_offset = pma_calc_ipa_offset(ipa_begin, alignment);
	for(uint64_t i = 0; i < PAGE_COUNT; i++)
	{
		if(pages[i] == 0)
		{
			page_count += 1;
			if (page_count >= BYTES_TO_PAGES(size)) {
				break;
			}
		} else {
			start_pn = i + 1;
			page_count = 0;
			if(alignment > 0)
			{
				align_incr = (1 << alignment) - (start_pn % (1 << alignment));
				start_pn += align_incr + align_offset;
				i += align_incr + align_offset;
			}
		}
	}

	if (start_pn >= PAGE_COUNT || page_count < BYTES_TO_PAGES(size)) {
		dlog_error("No sufficiently large memory chunk left.\n");
		return PN_TO_PTR(FAULT_PAGE_NUMBER);
	}

	sl_lock(&pages_spinlock);
    for(uint64_t i = start_pn; i < start_pn + page_count; i++) {
        pages[i] = id_bit;
    }

	for (uint64_t i = start_pn; i < start_pn + page_count; i++) {
		pages[i] = id_bit;
	}

	uint64_t end_pn = start_pn + page_count - 1;
	pages[end_pn] = (pages_t)(pages[end_pn] | ID_TO_LAST_PAGE_BIT(id));
	sl_unlock(&pages_spinlock);

	// If an identity mapping is desired, set the ipa equal to pa
	if (ipa_addr(ipa_begin) == PMA_IDENTITY_MAP)
	{
		ipa_begin = ipa_init(PN_TO_PTR(start_pn));
	}	

	dlog_debug("PMA Allocation %#x - %#x | IPA begin: %#x\n", PN_TO_PTR(start_pn), PN_TO_PTR(end_pn)+PAGE_SIZE-1, ipa_addr(ipa_begin));

/* Map memory to given ipa address if specified */
	ret_val = map_memory(p, ipa_begin, start_pn, end_pn, mode, id, ppool);
	
	

	// cache this unholy beast
	add_alloc_cache(ret_val, start_pn, end_pn, page_count, id);

	add_ids_used(id);

	return ret_val;
	// TODO: revert allocation if mapping didn't work
}

uintptr_t pma_hypervisor_alloc(size_t size, uint32_t mode)
{
	uintptr_t ptr = pma_aligned_alloc(hypervisor_ptable, ipa_init(PMA_IDENTITY_MAP), size, 0, mode, HYPERVISOR_ID, hypervisor_ppool);
	memset_s((void *) ptr, size, 0, size);
	return ptr;
}

// If pma_aligned_alloc fails we will try again with smaller chunk sizes
// this function returns only a pointer to one of the allocated regions and
// should thus only 
uintptr_t pma_aligned_alloc_with_split(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint8_t alignment, uint32_t mode, uint8_t id, struct mpool *ppool, uint8_t max_splits)
{	
	uintptr_t ret = pma_aligned_alloc(p, ipa_begin, size, alignment, mode, id, ppool);
	if(ret == PN_TO_PTR(FAULT_PAGE_NUMBER) && max_splits > 0 && size > PAGE_SIZE){
		dlog_debug("Retrying allocation in split chunks (%u more splits allowed).\n", max_splits);
		// must differentiate when LSB above page size is 1		
		size_t split_1_size = (size & (1 << PAGE_BITS)) ? (((size >> 1) & ~(PAGE_SIZE - 1))+ (1 << PAGE_BITS)): ((size >> 1) & ~(PAGE_SIZE - 1));
		size_t split_2_size = size - split_1_size;
		ipaddr_t split_2_ipa_begin = ipa_add(ipa_begin,split_1_size);
		ret = pma_aligned_alloc_with_split(p, ipa_begin, split_1_size, alignment, mode, id, ppool, (max_splits - 1));
		if (ret == PN_TO_PTR(FAULT_PAGE_NUMBER))
		{
			return ret;
		}
		ret = pma_aligned_alloc_with_split(p, split_2_ipa_begin, split_2_size, alignment, mode, id, ppool, (max_splits - 1));
	}

	return ret;
}

bool pma_hypervisor_assign(uintptr_t ptr, size_t size, uint32_t mode)
{
	return pma_assign(hypervisor_ptable, ptr,  ipa_init(PMA_IDENTITY_MAP), size, mode, HYPERVISOR_ID, hypervisor_ppool);
}

bool pma_assign(struct mm_ptable* p, uintptr_t ptr, ipaddr_t ipa_begin, size_t size, uint32_t mode, uint8_t id, struct mpool *ppool)
{

	// check whether a valid ID is provided
	if (!is_valid_id(id)) {
		return false;
	}

	if (size > MEMORY_SIZE) {
		dlog_error("Assigning memory of size %u not possible.\n", size);
		return false;
	}

	if(id == HYPERVISOR_ID && ipa_addr(ipa_begin) != PMA_IDENTITY_MAP){
		dlog_error("An IPA value has been given for an assignment to the hypervisor.\n");
		return false;
	}

	pages_t id_bit = ID_TO_BIT(id);

	uint64_t start_pn = PTR_TO_PN(ptr);

	if (start_pn > PAGE_COUNT) {
		dlog_error("Pointer (%p, start_pn: %u) exceeds page count.\n",
			   ptr, start_pn);
		return false;
	}

	if (is_restricted(start_pn)) {
		dlog_error("Illegal assign attempted to restricted section.\n");
		return false;
	}

	// check if the pointer points to an already allocated memory section
	if (pages[start_pn] == 0) {
		dlog_error(
			"Assigning an un-allocated memory region not possible, "
			"use pma_alloc instead.\n");
		return false;
	}

	// check if the memory region is already assigned to the provided ID
	if (pages[start_pn] & id_bit) {
		dlog_info("Memory region already assigned to ID 0x%02x.\n", id_bit);
		return true;
	}

	uint64_t end_pn = PTR_TO_PN(ptr + size - 1);
	sl_lock(&pages_spinlock);
	for (uint64_t i = start_pn; i <= end_pn; i++) {
		pages[i] = pages[i] | id_bit;
		if ((pages[i] & LAST_PAGE_BITS) && (i != end_pn)) {
			dlog_error(
				"Memory assignment spans multiple "
				"allocations.\n");
		}
	}
	pages[end_pn] = (pages_t)(pages[end_pn] | ID_TO_LAST_PAGE_BIT(id));
	sl_unlock(&pages_spinlock);

	if(map_memory(p, ipa_begin, start_pn, end_pn, mode, id, ppool) == PN_TO_PTR(FAULT_PAGE_NUMBER)) {
		//TODO: revert assignment
		return false;
	}

	struct alloc_cache_entry *entry = get_cached(ptr);

	if (entry) {
		entry->owner_ids |= ID_TO_BIT(id);
	}

	add_ids_used(id);

	return true;
}

/**
 * @brief Frees an allocated physical memory region.
 * 
 * @param p 
 * @param ptr 
 * @param id 
 * @param ppool 
 * @return true if the operation is successful
 * @return false otherwise
 */
bool pma_free(struct mm_ptable *p, uintptr_t ptr, uint8_t id, struct mpool *ppool)
{
	struct alloc_cache_entry *entry = get_cached(ptr);

	// check whether a valid ID is provided
	if (!is_valid_id(id)) {
		return false;
	}
	pages_t id_bit = ID_TO_BIT(id);

	uint64_t start_pn = get_start_page_number(ptr, id);
	if (is_restricted(start_pn)) {
		dlog_error("Illegal attempt to free a restricted section.\n");
		return false;
	}

	// check if the pointer points to an already allocated memory section
	if (pages[start_pn] == 0) {
		dlog_error("Freeing an un-allocated memory region not possible.\n");
		return false;
	}

	// check that the memory region is assigned to the provided ID
	if ((pages[start_pn] & id_bit) == 0) {
		dlog_error("Memory region is not assigned to ID 0x%02x.\n", id_bit);
		return false;
	}

	// If the memory chunk is *only* assigned to the provided ID then free
	// must be used
	/*if ((pages[start_pn] & ~id_bit) == 0) {
		dlog_error(
			"Memory region is not assigned to other IDs, use "
			"free.\n");
		return false;
	}*/

	if (entry) {
		entry->owner_ids &= ~ID_TO_BIT(id);  // remove ID
	}

	rem_ids_used(id);

	pages_t id_last_page_bit = ID_TO_LAST_PAGE_BIT(id);
	uint64_t end_pn = 0;
	sl_lock(&pages_spinlock);
	for (uint64_t i = start_pn; i < PAGE_COUNT; i++) {
		pages[i] = pages[i] & (~id_bit);
		if (pages[i] & id_last_page_bit) {
			pages[i] = pages[i] & (~id_last_page_bit);
			end_pn = i;
			break;
		}
	}
	sl_unlock(&pages_spinlock);

	if (unmap_memory(p, start_pn, end_pn, id, ppool) == PN_TO_PTR(FAULT_PAGE_NUMBER)) {
		// TODO: fix allocation in case unmapping didn't work (maybe the
		// memory wasn't allocated/mapped)
		return false;
	}

	return true;
}

bool pma_hypervisor_free(uintptr_t ptr)
{
	return pma_free(hypervisor_ptable, ptr, HYPERVISOR_ID, hypervisor_ppool);
}
