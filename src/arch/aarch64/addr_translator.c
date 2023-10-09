/*
 * Copyright 2021 Sanctuary
 * 
 * This file contains functions which perform address translations
 * between VA, IPA and PA
 */
#include "pg/arch/addr_translator.h"
#include "pg/spinlock.h"
#include "msr.h"


static struct spinlock par_el1_lock;

/*
 * Translates a virtual address into an intermediate physical address.
 * Returns ADDR_NOT_MAPPED if translation is not possible.
 */
ipaddr_t arch_translate_va_to_ipa(vaddr_t va){
    sl_lock(&par_el1_lock);
    __asm__ volatile("at s1e1r, " "%0" :: "r"(va_addr(va)));
    uintpaddr_t mapped_ipa;
    __asm__ volatile("mrs %0, " "PAR_EL1" : "=r"(mapped_ipa));
    sl_unlock(&par_el1_lock);
    if(mapped_ipa & PAR_FAIL_MASK){
        return ipa_init(ADDR_NOT_MAPPED);
    } 
    // add bits not in PAR from va to address
    return  ipa_init((mapped_ipa & PA_BITS_MASK) + (PAGE_BITS_MASK & va_addr(va)));
}

/**
 * This function performs a page table walk in software.
 */
static inline paddr_t arch_translate_ipa_to_pa_software(ipaddr_t ipa, struct mm_ptable ptable){
    paddr_t pa;
    if (ipa_addr(ipa) == ADDR_NOT_MAPPED)
    {
        return pa_init(ADDR_NOT_MAPPED);
    }    
    // value for table of root level of Stage 2 is stored in VTTBR_EL2
    if(!mm_vm_page_table_walk(&ptable, ipa, &pa)){
        return pa_init(ADDR_NOT_MAPPED);
    }   

    return pa; 
}

/*
 * Translates an intermediate physical address into a physical address.
 * Requires the ptable for Stage 2 translation.
 * Returns ADDR_NOT_MAPPED if translation is not possible.
 */
paddr_t arch_translate_ipa_to_pa(ipaddr_t ipa, struct mm_ptable ptable){
    return arch_translate_ipa_to_pa_software(ipa, ptable); // sadly no direct translation for stage 2 possible on aarch64
}

/*
 * Translates a virtual address into a physical address.
 * Requires the ptable for Stage 2 translation.
 * Returns ADDR_NOT_MAPPED if translation is not possible.
 */
paddr_t arch_translate_va_to_pa(vaddr_t va, struct mm_ptable ptable){
    uintreg_t par_el1;

    sl_lock(&par_el1_lock);
    __asm__ volatile("at s12e1r, " "%0" :: "r"(va_addr(va)));
    par_el1 = read_msr(PAR_EL1);
    sl_unlock(&par_el1_lock);

    if (par_el1 & PAR_FAIL_MASK){
        if ((par_el1 & PAR_STAGE_MASK) && (((par_el1 & PAR_FST_MASK) >> 3)== PAR_PERMISSION_FAULT_NO_LVL)){
            // Stage 2 translation failed because of missing permission 
            return arch_translate_ipa_to_pa_software(arch_translate_va_to_ipa(va), ptable);
        }
        return pa_init(ADDR_NOT_MAPPED);
    }

    // translation was successful, apply address mask and add lower bits
    return pa_init((par_el1 & PA_BITS_MASK) + (PAGE_BITS_MASK & va_addr(va))); 
}

/* If arg1 and arg2 contains an address, translate it from IPA to PA 
 * If the args do not contain an address from the VMs memory, do nothing
 * TODO: do other arg fields also contain addresses?
 * NOTE: If the args are returned to the VM, this leaks information about the physical address
 * So originals address value must be preserved beforehand
 */		
void arch_translate_addr_args(struct vm *vm, struct ffa_value *args){
        uintpaddr_t addr_arg = (args->arg1 << 32) | args->arg2;
		if (addr_arg >= ipa_addr(vm->ipa_mem_begin) && addr_arg < ipa_addr(vm->ipa_mem_end))
		{
			addr_arg = pa_addr(arch_translate_ipa_to_pa(ipa_init(addr_arg), vm->ptable));
			args->arg1 = addr_arg >> 32;
			args->arg2 = addr_arg & 0xFFFFFFFF;
		}
}
