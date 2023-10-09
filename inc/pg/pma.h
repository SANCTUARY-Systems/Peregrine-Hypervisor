/*
 * Copyright 2021 Sanctuary
 */

#pragma once

#include "pg/arch/types.h"
#include "pg/mm.h"

#define pages_t uint16_t
#define MAX_IDS (sizeof(pages_t)*8/2)

#define PMA_ALIGN_AUTO_PAGE_LVL UINT8_MAX
#define PMA_IDENTITY_MAP 0xDEADDEAD

/*version for handling two memory sections, specific for the FVP where the second memory region starts at 0x880000000*/
//#define PN_TO_PTR(pn) (((uintptr_t)((uint64_t)START_ADDRESS1 + ((pn) * PAGE_SIZE))) + (~(((BYTES_TO_PAGES(MEMORY_SIZE1)-pn) >> 63) & 0x1))*(-START_ADDRESS2+END_ADDRESS1))
//#define PTR_TO_PN(ptr) (((uint64_t)(((uintptr_t)(ptr & 0xFFFFFFFF)) - START_ADDRESS1 + ((ptr >> 35)*(MEMORY_SIZE1)))) / PAGE_SIZE)
//#define START_ADDRESS1 ((uintptr_t)0x80000000LL) //starting memory address of the physical memory to be managed, must be page-aligned
//#define END_ADDRESS1 ((uintptr_t)0x100000000LL)
//#define MEMORY_SIZE1 ((END_ADDRESS1 - START_ADDRESS1) + 1)
//#define START_ADDRESS2 ((uintptr_t)0x880000000LL) //starting memory address of the physical memory to be managed, must be page-aligned
//#define END_ADDRESS2 ((uintptr_t)0x900000000LL)
//#define MEMORY_SIZE2 (END_ADDRESS2 - START_ADDRESS2)
//#define MEMORY_SIZE ((uint64_t)(MEMORY_SIZE1 + MEMORY_SIZE2)) //size of the memory for which memory allocations should be managed

#define PN_TO_PTR(pn) ((uintptr_t)((uint64_t)phys_start_address + ((pn) * PAGE_SIZE)))
#define PTR_TO_PN(ptr) (((uint64_t)(((uintptr_t)(ptr)) - phys_start_address)) / PAGE_SIZE)
#define BYTES_TO_PAGES(bytes) (((bytes) + PAGE_SIZE - 1) / PAGE_SIZE)
#define PAGES_TO_BYTES(page_count) ((page_count) * PAGE_SIZE)
#define ID_TO_BIT(id) (pages_t)(1 << ((id)*2))
#define ID_TO_LAST_PAGE_BIT(id) (ID_TO_BIT(id) << 1)


//#define PAGE_SIZE 4096 //size of a memory page, also unit of allocation
//TODO: get start address and memory size automatically
#ifdef PHYS_START_ADDR
#define START_ADDRESS PHYS_START_ADDR //starting memory address of the physical memory to be managed, must be page-aligned
#else
#define START_ADDRESS 0x80000000 //starting memory address of the physical memory to be managed, must be page-aligned
#define PHYS_MEM_SIZE 0x80000000 
#endif

#define MEMORY_SIZE ((uint64_t) PHYS_MEM_SIZE) //size of the memory for which memory allocations should be managed

#define PAGE_COUNT BYTES_TO_PAGES(MEMORY_SIZE) //number of memory pages
#define FAULT_PAGE_NUMBER 0 //page number of a page that should be used to indicate errors, typically the first page aka NULL

#define LAST_PAGE_BITS (pages_t)(0xAAAAAAAA)
//#define LAST_PAGE_MASK (pages_t)~LAST_PAGE_BIT

#define HYPERVISOR_ID (uint8_t)0

#if defined HOST_TESTING_MODE && HOST_TESTING_MODE != 0
pages_t *pma_early_set_start_addr(uintptr_t start_addr);
#endif

uintptr_t pma_get_fault_ptr();
//void print_allocations(uint64_t num); //for debugging only
bool is_assigned(uintptr_t ptr, uint8_t id);
void pma_print_chunks();
uintptr_t pma_get_start(uintptr_t ptr, uint8_t id);
size_t pma_get_size(uintptr_t ptr, uint8_t id);
bool pma_is_assigned(uintptr_t ptr, size_t size, uint8_t id);
bool pma_init(struct mm_stage1_locked stage1_locked, struct mpool *ppool);
void pma_update_pool(struct mpool *ppool);
uintptr_t pma_hypervisor_alloc(size_t size, uint32_t mode);
uintptr_t pma_alloc(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint32_t mode, uint8_t id, struct mpool *ppool);
//Alignment n is the number 2^n of pages that the beginning of the allocation needs to be aligned to
uintptr_t pma_aligned_alloc(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint8_t alignment, uint32_t mode, uint8_t id, struct mpool *ppool);
uintptr_t pma_aligned_alloc_with_split(struct mm_ptable* p, ipaddr_t ipa_begin, size_t size, uint8_t alignment, uint32_t mode, uint8_t id, struct mpool *ppool, uint8_t max_splits);
bool pma_free(struct mm_ptable* p, uintptr_t ptr, uint8_t id, struct mpool *ppool);
bool pma_assign(struct mm_ptable* p, uintptr_t ptr, ipaddr_t ipa_begin, size_t size, uint32_t mode, uint8_t id, struct mpool *ppool);
bool pma_reserve_memory(uintptr_t begin, uintptr_t end, uint8_t id);
bool pma_release_memory(uintptr_t begin, uintptr_t end, uint8_t id);
bool pma_hypervisor_free(uintptr_t ptr);
bool pma_hypervisor_assign(uintptr_t ptr, size_t size, uint32_t mode);

/*
 * Wrapper functions
 */

