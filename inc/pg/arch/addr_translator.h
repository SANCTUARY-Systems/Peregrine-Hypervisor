/*
 * Copyright 2021 Sanctuary
 * 
 * This file contains functions which perform address translations
 * between VA, IPA and PA
 */
#pragma once
#include "pg/mm.h"
#include "pg/vm.h"


#define PAR_FAIL_MASK (1 << 0) // 0 = successful and 1 = failure
#define PAR_STAGE_MASK (1 << 9) // Indicates stage of failure 0 = Stage 1 and 1 = Stage 2
#define PAR_FST_MASK 0x7E
#define PAR_PERMISSION_FAULT_NO_LVL 0x3
#define ADDR_NOT_MAPPED 0xFFFFFFFFFFFF

#define vm_ipa_to_pa(ptr, id) arch_translate_ipa_to_pa(ipa_init((uintptr_t)(ptr)), vm_find(id)->ptable).pa

ipaddr_t arch_translate_va_to_ipa(vaddr_t va);
paddr_t arch_translate_ipa_to_pa(ipaddr_t ipa, struct mm_ptable ptable);
paddr_t arch_translate_va_to_pa(vaddr_t va, struct mm_ptable ptable);
void arch_translate_addr_args(struct vm *vm, struct ffa_value *args);
