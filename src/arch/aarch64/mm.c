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

#include "pg/mm.h"

#include "pg/arch/barriers.h"
#include "pg/arch/cpu.h"
#include "pg/arch/mmu.h"

#include "pg/dlog.h"

#include "msr.h"
#include "sysregs.h"

/* Keep macro alignment */
/* clang-format off */

#define NON_SHAREABLE   UINT64_C(0)
#define OUTER_SHAREABLE UINT64_C(2)
#define INNER_SHAREABLE UINT64_C(3)

#define PTE_VALID        (UINT64_C(1) << 0)
#define PTE_LEVEL0_BLOCK (UINT64_C(1) << 1)
#define PTE_TABLE        (UINT64_C(1) << 1)

#define STAGE1_XN          (UINT64_C(1) << 54)
#define STAGE1_PXN         (UINT64_C(1) << 53)
#define STAGE1_CONTIGUOUS  (UINT64_C(1) << 52)
#define STAGE1_DBM         (UINT64_C(1) << 51)
#define STAGE1_GP          (UINT64_C(1) << 50)
#define STAGE1_NG          (UINT64_C(1) << 11)
#define STAGE1_AF          (UINT64_C(1) << 10)
#define STAGE1_SH(x)       ((x) << 8)
#define STAGE1_AP2         (UINT64_C(1) << 7)
#define STAGE1_AP1         (UINT64_C(1) << 6)
#define STAGE1_AP(x)       ((x) << 6)
#define STAGE1_NS          (UINT64_C(1) << 5)
#define STAGE1_ATTRINDX(x) ((x) << 2)

#define STAGE1_READONLY  UINT64_C(2)
#define STAGE1_READWRITE UINT64_C(0)

#define STAGE1_DEVICEINDX UINT64_C(0)
#define STAGE1_NORMALINDX UINT64_C(1)

#define STAGE2_XN(x)      ((x) << 53)
#define STAGE2_CONTIGUOUS (UINT64_C(1) << 52)
#define STAGE2_DBM        (UINT64_C(1) << 51)
#define STAGE2_AF         (UINT64_C(1) << 10)
#define STAGE2_SH(x)      ((x) << 8)
#define STAGE2_S2AP(x)    ((x) << 6)

#define STAGE2_EXECUTE_ALL  UINT64_C(0)
#define STAGE2_EXECUTE_EL0  UINT64_C(1)
#define STAGE2_EXECUTE_NONE UINT64_C(2)
#define STAGE2_EXECUTE_EL1  UINT64_C(3)
#define STAGE2_EXECUTE_MASK UINT64_C(3)

/* Table attributes only apply to stage 1 translations. */
#define TABLE_NSTABLE  (UINT64_C(1) << 63)
#define TABLE_APTABLE1 (UINT64_C(1) << 62)
#define TABLE_APTABLE0 (UINT64_C(1) << 61)
#define TABLE_XNTABLE  (UINT64_C(1) << 60)
#define TABLE_PXNTABLE (UINT64_C(1) << 59)

/* The following are stage-2 software defined attributes. */
#define STAGE2_SW_OWNED     (UINT64_C(1) << 55)
#define STAGE2_SW_EXCLUSIVE (UINT64_C(1) << 56)

/* The following are stage-2 memory attributes for normal memory. */
#define STAGE2_DEVICE_MEMORY UINT64_C(0)
#define STAGE2_NONCACHEABLE  UINT64_C(1)
#define STAGE2_WRITETHROUGH  UINT64_C(2)
#define STAGE2_WRITEBACK     UINT64_C(3)

/* The following are stage-2 memory attributes for device memory. */
#define STAGE2_MEMATTR_DEVICE_nGnRnE UINT64_C(0)
#define STAGE2_MEMATTR_DEVICE_nGnRE  UINT64_C(1)
#define STAGE2_MEMATTR_DEVICE_nGRE   UINT64_C(2)
#define STAGE2_MEMATTR_DEVICE_GRE    UINT64_C(3)

/* The following construct and destruct stage-2 memory attributes. */
#define STAGE2_MEMATTR(outer, inner) ((((outer) << 2) | (inner)) << 2)
#define STAGE2_MEMATTR_TYPE_MASK UINT64_C(3 << 4)

#define STAGE2_ACCESS_READ  UINT64_C(1)
#define STAGE2_ACCESS_WRITE UINT64_C(2)

#define CACHE_WORD_SIZE 4

/**
 * Threshold number of pages in TLB to invalidate after which we invalidate all
 * TLB entries on a given level.
 * Constant is the number of pointers per page table entry, also used by Linux.
 */
#define MAX_TLBI_OPS  MM_PTE_PER_PAGE

/* clang-format on */

#define tlbi(op)                               \
	do {                                   \
		__asm__ volatile("tlbi " #op); \
	} while (0)
#define tlbi_reg(op, reg)                                              \
	do {                                                           \
		__asm__ __volatile__("tlbi " #op ", %0" : : "r"(reg)); \
	} while (0)

/** Mask for the address bits of the pte. */
#define PTE_ADDR_MASK \
	(((UINT64_C(1) << 48) - 1) & ~((UINT64_C(1) << PAGE_BITS) - 1))

/** Mask for the attribute bits of the pte. */
#define PTE_ATTR_MASK (~(PTE_ADDR_MASK | (UINT64_C(1) << 1)))

/**
 * Configuration information for memory management. Order is important as this
 * is read from assembly.
 *
 * It must only be written to from `arch_mm_init()` to avoid cache and
 * synchronization problems.
 */
struct arch_mm_config {
	uintreg_t ttbr0_el2;
	uintreg_t vtcr_el2;
	uintreg_t mair_el2;
	uintreg_t tcr_el2;
	uintreg_t sctlr_el2;
	uintreg_t vstcr_el2;
	uintreg_t hcr_el2;
} arch_mm_config;

static uint8_t mm_s2_max_level;
static uint8_t mm_s2_root_table_count;

/**
 * Returns the encoding of a page table entry that isn't present.
 */
pte_t arch_mm_absent_pte(uint8_t level)
{
	(void)level;
	return 0;
}

/**
 * Converts a physical address to a table PTE.
 *
 * The spec says that 'Table descriptors for stage 2 translations do not
 * include any attribute field', so we don't take any attributes as arguments.
 */
pte_t arch_mm_table_pte(uint8_t level, paddr_t pa)
{
	/* This is the same for all levels on aarch64. */
	(void)level;
	return pa_addr(pa) | PTE_TABLE | PTE_VALID;
}

/**
 * Converts a physical address to a block PTE.
 *
 * The level must allow block entries.
 */
pte_t arch_mm_block_pte(uint8_t level, paddr_t pa, uint64_t attrs)
{
	pte_t pte = pa_addr(pa) | attrs;

	if (level == 0) {
		/* A level 0 'block' is actually a page entry. */
		pte |= PTE_LEVEL0_BLOCK;
	}
	return pte;
}

/**
 * Specifies whether block mappings are acceptable at the given level.
 *
 * Level 0 must allow block entries.
 */
bool arch_mm_is_block_allowed(uint8_t level)
{
	return level <= 2;
}

/**
 * Determines if the given pte is present, i.e., if it is valid or it is invalid
 * but still holds state about the memory so needs to be present in the table.
 */
bool arch_mm_pte_is_present(pte_t pte, uint8_t level)
{
	return arch_mm_pte_is_valid(pte, level) || (pte & STAGE2_SW_OWNED) != 0;
}

/**
 * Determines if the given pte is valid, i.e., if it points to another table,
 * to a page, or a block of pages that can be accessed.
 */
bool arch_mm_pte_is_valid(pte_t pte, uint8_t level)
{
	(void)level;
	return (pte & PTE_VALID) != 0;
}

/**
 * Determines if the given pte references a block of pages.
 */
bool arch_mm_pte_is_block(pte_t pte, uint8_t level)
{
	/* We count pages at level 0 as blocks. */
	return arch_mm_is_block_allowed(level) &&
	       (level == 0 ? (pte & PTE_LEVEL0_BLOCK) != 0
			   : arch_mm_pte_is_present(pte, level) &&
				     !arch_mm_pte_is_table(pte, level));
}

/**
 * Determines if the given pte references another table.
 */
bool arch_mm_pte_is_table(pte_t pte, uint8_t level)
{
	return level != 0 && arch_mm_pte_is_valid(pte, level) &&
	       (pte & PTE_TABLE) != 0;
}

static uint64_t pte_addr(pte_t pte)
{
	return pte & PTE_ADDR_MASK;
}

/**
 * Clears the given physical address, i.e., clears the bits of the address that
 * are not used in the pte.
 */
paddr_t arch_mm_clear_pa(paddr_t pa)
{
	return pa_init(pte_addr(pa_addr(pa)));
}

/**
 * Extracts the physical address of the block referred to by the given page
 * table entry.
 */
paddr_t arch_mm_block_from_pte(pte_t pte, uint8_t level)
{
	(void)level;
	return pa_init(pte_addr(pte));
}

/**
 * Extracts the physical address of the page table referred to by the given page
 * table entry.
 */
paddr_t arch_mm_table_from_pte(pte_t pte, uint8_t level)
{
	(void)level;
	return pa_init(pte_addr(pte));
}

/**
 * Extracts the architecture-specific attributes applies to the given page table
 * entry.
 */
uint64_t arch_mm_pte_attrs(pte_t pte, uint8_t level)
{
	(void)level;
	return pte & PTE_ATTR_MASK;
}

/**
 * Execute any barriers or synchronization that is required
 * by a given architecture, after page table writes.
 */
void arch_mm_sync_table_writes(void)
{
	/*
	 * Ensure visibility of table updates to translation table walks.
	 */
	dsb(ish);
}

/**
 * Invalidates stage-1 TLB entries referring to the given virtual address range.
 */
void arch_mm_invalidate_stage1_range(vaddr_t va_begin, vaddr_t va_end)
{
	uintvaddr_t begin = va_addr(va_begin);
	uintvaddr_t end = va_addr(va_end);
	uintvaddr_t it;

	/* Sync with page table updates. */
	arch_mm_sync_table_writes();

	/*
	 * Revisions prior to Armv8.4 do not support invalidating a range of
	 * addresses, which means we have to loop over individual pages. If
	 * there are too many, it is quicker to invalidate all TLB entries.
	 */
	if ((end - begin) > (MAX_TLBI_OPS * PAGE_SIZE)) {
		if (VM_TOOLCHAIN == 1) {
			tlbi(vmalle1is);
		} else {
			tlbi(alle2is);
		}
	} else {
		begin >>= 12;
		end >>= 12;
		/* Invalidate stage-1 TLB, one page from the range at a time. */
		for (it = begin; it < end;
		     it += (UINT64_C(1) << (PAGE_BITS - 12))) {
			if (VM_TOOLCHAIN == 1) {
				tlbi_reg(vae1is, it);
			} else {
				tlbi_reg(vae2is, it);
			}
		}
	}

	/* Sync data accesses with TLB invalidation completion. */
	dsb(ish);

	/* Sync instruction fetches with TLB invalidation completion. */
	isb();
}

/**
 * Invalidates stage-2 TLB entries referring to the given intermediate physical
 * address range.
 */
void arch_mm_invalidate_stage2_range(ipaddr_t va_begin, ipaddr_t va_end)
{
	uintpaddr_t begin = ipa_addr(va_begin);
	uintpaddr_t end = ipa_addr(va_end);
	uintpaddr_t it;

	/* TODO: This only applies to the current VMID. */

	/* Sync with page table updates. */
	arch_mm_sync_table_writes();

	/*
	 * Revisions prior to Armv8.4 do not support invalidating a range of
	 * addresses, which means we have to loop over individual pages. If
	 * there are too many, it is quicker to invalidate all TLB entries.
	 */
	if ((end - begin) > (MAX_TLBI_OPS * PAGE_SIZE)) {
		/*
		 * Invalidate all stage-1 and stage-2 entries of the TLB for
		 * the current VMID.
		 */
		tlbi(vmalls12e1is);
	} else {
		begin >>= 12;
		end >>= 12;

		/*
		 * Invalidate stage-2 TLB, one page from the range at a time.
		 * Note that this has no effect if the CPU has a TLB with
		 * combined stage-1/stage-2 translation.
		 */
		for (it = begin; it < end;
		     it += (UINT64_C(1) << (PAGE_BITS - 12))) {
			tlbi_reg(ipas2e1is, it);
		}

		/*
		 * Ensure completion of stage-2 invalidation in case a page
		 * table walk on another CPU refilled the TLB with a complete
		 * stage-1 + stage-2 walk based on the old stage-2 mapping.
		 */
		dsb(ish);

		/*
		 * Invalidate all stage-1 TLB entries. If the CPU has a combined
		 * TLB for stage-1 and stage-2, this will invalidate stage-2 as
		 * well.
		 */
		tlbi(vmalle1is);
	}

	/* Sync data accesses with TLB invalidation completion. */
	dsb(ish);

	/* Sync instruction fetches with TLB invalidation completion. */
	isb();
}

/**
 * Returns the smallest cache line size of all the caches for this core.
 */
static uint16_t arch_mm_dcache_line_size(void)
{
	return CACHE_WORD_SIZE *
	       (UINT16_C(1) << ((read_msr(CTR_EL0) >> 16) & 0xf));
}

void arch_mm_flush_dcache(void *base, size_t size)
{
	/* Clean and invalidate each data cache line in the range. */
	uint16_t line_size = arch_mm_dcache_line_size();
	uintptr_t line_begin = (uintptr_t)base & ~(line_size - 1);
	uintptr_t end = (uintptr_t)base + size;

	while (line_begin < end) {
		__asm__ volatile("dc civac, %0" : : "r"(line_begin));
		line_begin += line_size;
	}
	dsb(sy);
}

uint64_t arch_mm_mode_to_stage1_attrs(uint32_t mode)
{
	uint64_t attrs = 0;

	attrs |= STAGE1_AF | STAGE1_SH(INNER_SHAREABLE);

	/* Define the execute bits. */
	if (!(mode & MM_MODE_X)) {
		attrs |= STAGE1_XN;
	}
#if BRANCH_PROTECTION
	else {
		/* Mark code pages as Guarded Pages if BTI is supported. */
		if (is_arch_feat_bti_supported()) {
			attrs |= STAGE1_GP;
		}
	}
#endif

	/* Define the read/write bits. */
	if (mode & MM_MODE_W) {
		attrs |= STAGE1_AP(STAGE1_READWRITE);
	} else {
		attrs |= STAGE1_AP(STAGE1_READONLY);
	}

	/* Define the memory attribute bits. */
	if (mode & MM_MODE_D) {
		attrs |= STAGE1_ATTRINDX(STAGE1_DEVICEINDX);
	} else {
		attrs |= STAGE1_ATTRINDX(STAGE1_NORMALINDX);
	}

	/* Define the valid bit. */
	if (!(mode & MM_MODE_INVALID)) {
		attrs |= PTE_VALID;
	}

	return attrs;
}

uint64_t arch_mm_mode_to_stage2_attrs(uint32_t mode)
{
	uint64_t attrs = 0;
	uint64_t access = 0;

	/*
	 * Non-shareable is the "neutral" share mode, i.e., the shareability
	 * attribute of stage 1 will determine the actual attribute.
	 */
	attrs |= STAGE2_AF | STAGE2_SH(NON_SHAREABLE);

	/* Define the read/write bits. */
	if (mode & MM_MODE_R) {
		access |= STAGE2_ACCESS_READ;
	}

	if (mode & MM_MODE_W) {
		access |= STAGE2_ACCESS_WRITE;
	}

	attrs |= STAGE2_S2AP(access);

	/* Define the execute bits. */
	if (mode & MM_MODE_X) {
		attrs |= STAGE2_XN(STAGE2_EXECUTE_ALL);
	} else {
		attrs |= STAGE2_XN(STAGE2_EXECUTE_NONE);
	}

	/*
	 * Define the memory attribute bits, using the "neutral" values which
	 * give the stage-1 attributes full control of the attributes.
	 */
	if (mode & MM_MODE_D) {
		attrs |= STAGE2_MEMATTR(STAGE2_DEVICE_MEMORY,
					STAGE2_MEMATTR_DEVICE_GRE);
	} else {
		attrs |= STAGE2_MEMATTR(STAGE2_WRITEBACK, STAGE2_WRITEBACK);
	}

	/* Define the ownership bit. */
	if (!(mode & MM_MODE_UNOWNED)) {
		attrs |= STAGE2_SW_OWNED;
	}

	/* Define the exclusivity bit. */
	if (!(mode & MM_MODE_SHARED)) {
		attrs |= STAGE2_SW_EXCLUSIVE;
	}

	/* Define the valid bit. */
	if (!(mode & MM_MODE_INVALID)) {
		attrs |= PTE_VALID;
	}

	return attrs;
}

uint32_t arch_mm_stage2_attrs_to_mode(uint64_t attrs)
{
	uint32_t mode = 0;

	if (attrs & STAGE2_S2AP(STAGE2_ACCESS_READ)) {
		mode |= MM_MODE_R;
	}

	if (attrs & STAGE2_S2AP(STAGE2_ACCESS_WRITE)) {
		mode |= MM_MODE_W;
	}

	if ((attrs & STAGE2_XN(STAGE2_EXECUTE_MASK)) ==
	    STAGE2_XN(STAGE2_EXECUTE_ALL)) {
		mode |= MM_MODE_X;
	}

	if ((attrs & STAGE2_MEMATTR_TYPE_MASK) == STAGE2_DEVICE_MEMORY) {
		mode |= MM_MODE_D;
	}

	if (!(attrs & STAGE2_SW_OWNED)) {
		mode |= MM_MODE_UNOWNED;
	}

	if (!(attrs & STAGE2_SW_EXCLUSIVE)) {
		mode |= MM_MODE_SHARED;
	}

	if (!(attrs & PTE_VALID)) {
		mode |= MM_MODE_INVALID;
	}

	return mode;
}

uint8_t arch_mm_stage1_max_level(void)
{
	/*
	 * For stage 1 we hard-code this to 2 for now so that we can
	 * save one page table level at the expense of limiting the
	 * physical memory to 512GB.
	 */
	return 2;
}

uint8_t arch_mm_stage2_max_level(void)
{
	return mm_s2_max_level;
}

uint8_t arch_mm_stage1_root_table_count(void)
{
	/* Stage 1 doesn't concatenate tables. */
	return 1;
}

uint8_t arch_mm_stage2_root_table_count(void)
{
	return mm_s2_root_table_count;
}

/**
 * Given the attrs from a table at some level and the attrs from all the blocks
 * in that table, returns equivalent attrs to use for a block which will replace
 * the entire table.
 */
uint64_t arch_mm_combine_table_entry_attrs(uint64_t table_attrs,
					   uint64_t block_attrs)
{
	/*
	 * Only stage 1 table descriptors have attributes, but the bits are res0
	 * for stage 2 table descriptors so this code is safe for both.
	 */
	if (table_attrs & TABLE_NSTABLE) {
		block_attrs |= STAGE1_NS;
	}
	if (table_attrs & TABLE_APTABLE1) {
		block_attrs |= STAGE1_AP2;
	}
	if (table_attrs & TABLE_APTABLE0) {
		block_attrs &= ~STAGE1_AP1;
	}
	if (table_attrs & TABLE_XNTABLE) {
		block_attrs |= STAGE1_XN;
	}
	if (table_attrs & TABLE_PXNTABLE) {
		block_attrs |= STAGE1_PXN;
	}
	return block_attrs;
}

/**
 * This is called early in initialization without MMU or caches enabled.
 */
bool arch_mm_init(paddr_t table)
{
	static const int pa_bits_table[16] = {32, 36, 40, 42, 44, 48};
	uint64_t features = read_msr(id_aa64mmfr0_el1);
	uint64_t pe_features = read_msr(id_aa64pfr0_el1);
	unsigned int nsa_nsw;
	int pa_bits = pa_bits_table[features & 0xf];
	int extend_bits;
	int sl0;

	/* Check that 4KB granules are supported. */
	if ((features >> 28) & 0xf) {
		dlog_error("4KB granules are not supported\n");
		return false;
	}

	/* Check the physical address range. */
	if (!pa_bits) {
		dlog_error(
			"Unsupported value of id_aa64mmfr0_el1.PARange: %x\n",
			features & 0xf);
		return false;
	}

	dlog_debug("Supported bits in physical address: %d\n", pa_bits);

	/*
	 * Determine sl0, starting level of the page table, based on the number
	 * of bits. The value is chosen to give the shallowest tree by making
	 * use of concatenated translation tables.
	 *
	 *  - 0 => start at level 1
	 *  - 1 => start at level 2
	 *  - 2 => start at level 3
	 */
	if (pa_bits >= 44) {
		sl0 = 2;
		mm_s2_max_level = 3;
	} else if (pa_bits >= 35) {
		sl0 = 1;
		mm_s2_max_level = 2;
	} else {
		sl0 = 0;
		mm_s2_max_level = 1;
	}

	/*
	 * Since the shallowest possible tree is used, the maximum number of
	 * concatenated tables must be used. This means if no more than 4 bits
	 * are used from the next level, they are instead used to index into the
	 * concatenated tables.
	 */
	extend_bits = ((pa_bits - PAGE_BITS) % PAGE_LEVEL_BITS);
	if (extend_bits > 4) {
		extend_bits = 0;
	}
	mm_s2_root_table_count = 1 << extend_bits;

	dlog_debug(
		"Stage 2 has %d page table levels with %d pages at the root.\n",
		mm_s2_max_level + 1, mm_s2_root_table_count);

	/*
	 * If the PE implements S-EL2 then VTCR_EL2.NSA/NSW bits are significant
	 * in secure state. In non-secure state, NSA/NSW behave as if set to
	 * 11b. If S-EL2 is not implemented NSA/NSW bits are RES0.
	 */
	if (((pe_features >> 36) & 0xF) == 1) {
		/*
		 * NSA/NSW=10b: in secure state,
		 * S2 translations for the NS IPA space access the NS PA space.
		 * S2 translation table walks for the NS IPA space are to the
		 * secure PA space.
		 */
		nsa_nsw = 2;
	} else {
		nsa_nsw = 0;
	}

	arch_mm_config = (struct arch_mm_config){
		.ttbr0_el2 = pa_addr(table),

		.vtcr_el2 =
			(1U << 31) |		   /* RES1. */
			(nsa_nsw << 29) |	   /* NSA/NSW. */
			((features & 0xf) << 16) | /* PS, matching features. */
			(0 << 14) |		   /* TG0: 4 KB granule. */
			(3 << 12) |		   /* SH0: inner shareable. */
			(1 << 10) |  /* ORGN0: normal, cacheable ... */
			(1 << 8) |   /* IRGN0: normal, cacheable ... */
			(sl0 << 6) | /* SL0. */
			((64 - pa_bits) << 0) | /* T0SZ: dependent on PS. */
			0,

		/*
		 * 0    -> Device-nGnRnE memory
		 * 0xff -> Normal memory, Inner/Outer Write-Back Non-transient,
		 *         Write-Alloc, Read-Alloc.
		 */
		.mair_el2 = (0 << (8 * STAGE1_DEVICEINDX)) |
			    (0xff << (8 * STAGE1_NORMALINDX)),

		.sctlr_el2 = get_sctlr_el2_value(),
		.vstcr_el2 = (1U << 31) |	    /* RES1. */
			     (0 << 30) |	    /* SA. */
			     (0 << 29) |	    /* SW. */
			     (0 << 14) |	    /* TG0: 4 KB granule. */
			     (sl0 << 6) |	    /* SL0. */
			     ((64 - pa_bits) << 0), /* T0SZ: dependent on PS. */
	};

	/*
	 * Configure tcr_el2 and hcr_el2. The configuration depends on whether
	 * VHE support is enabled by the build and is available in HW. If VHE is
	 * enabled and available, hcr_el2.e2h is set during boot, before the MMU
	 * is turned on. This is because setting e2h redefines registers, can be
	 * cached in the TLBs and enables the use of ttbr1_el2, among other
	 * things, which makes enabling it at run time much more complicated.
	 * The bit is set once during boot and is not expected to change for the
	 * boot cycle. When VHE is enabled, currently, only the lower virtual
	 * address range (ttbr0_el2) is used and the upper address
	 * range(ttbr0_el1) is disabled. This keeps Peregrine simple and
	 * consistent with its behavior when VHE is not enabled. When VHE is
	 * not enabled, hcr_el2 will default to 0 and will be set up during vCPU
	 * initialization.
	 */
	arch_mm_config.hcr_el2 = 0;
	if (has_vhe_support()) {
		arch_mm_config.hcr_el2 |= (HCR_EL2_E2H | HCR_EL2_TGE);
		arch_mm_config.tcr_el2 =
			(1UL << 38) | /* TBI1, top byte ignored. */
			(1UL << 37) | /* TBI0, top byte ignored. */
			(2UL << 32) | /* IPS, IPA size */
			(2UL << 30) | /* TG1, granule size, 4KB. */
			(3UL << 28) | /* SH1, inner shareable. */
			(1UL
			 << 26) | /* ORGN1, normal mem, WB RA WA Cacheable. */
			(1UL
			 << 24) | /* IRGN1, normal mem, WB RA WA Cacheable. */
			(1UL << 23) | /* EPD1 - Disable TTBR1_EL2 translation */
			(0UL << 22) | /* TTBR0_EL2.ASID defines ASID */
			(25UL << 16) | /* T1SZ, input address is 2^39 bytes. */
			(0UL << 14) |  /* TG0, granule size, 4KB. */
			(3UL << 12) |  /* SH0, inner shareable. */
			(1UL
			 << 10) | /* ORGN0, normal mem, WB RA WA Cacheable. */
			(1UL
			 << 8) | /* IRGN0, normal mem, WB RA WA Cacheable. */
			(25UL << 0) | /* T0SZ, input address is 2^39 bytes. */
			0;
	} else {
		arch_mm_config.tcr_el2 =
			(1 << 20) |		   /* TBI, top byte ignored. */
			((features & 0xf) << 16) | /* PS. */
			(0 << 14) |		   /* TG0, granule size, 4KB. */
			(3 << 12) |		   /* SH0, inner shareable. */
			(1 << 10) | /* ORGN0, normal mem, WB RA WA Cacheable. */
			(1 << 8) |  /* IRGN0, normal mem, WB RA WA Cacheable. */
			(25 << 0) | /* T0SZ, input address is 2^39 bytes. */
			0;
	}
	struct arch_mm_config* cp;
	cp = &arch_mm_config;
	dlog_debug("ttbr0_el2: 0x%#x\n", cp->ttbr0_el2);
	dlog_debug("vtcr_el2: 0x%#x\n", cp->vtcr_el2);
	dlog_debug("mair_el2: 0x%#x\n", cp->mair_el2);
	dlog_debug("tcr_el2: 0x%#x\n", cp->tcr_el2);
	dlog_debug("sctlr_el2: 0x%#x\n", cp->sctlr_el2);
	dlog_debug("vstcr_el2: 0x%#x\n", cp->vstcr_el2);
	dlog_debug("hcr_el2: 0x%#x\n", cp->hcr_el2);

	return true;
}

/**
 * Return the arch specific mm mode for send/recv pages of given VM ID.
 */
uint32_t arch_mm_extra_attributes_from_vm(uint16_t id)
{
	return (id == PG_HYPERVISOR_VM_ID) ? MM_MODE_NS : 0;
}
