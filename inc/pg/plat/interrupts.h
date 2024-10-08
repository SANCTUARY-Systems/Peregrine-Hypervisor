/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

/* New: */
#include "pg/fdt.h"
#include "pg/manifest.h"
#include "pg/vm.h"

#define DEFERRED_INT_ID 0xFFFF 

bool plat_interrupts_controller_driver_init(
	const struct fdt *fdt, struct mm_stage1_locked stage1_locked,
	struct mpool *ppool);
void plat_interrupts_controller_hw_init(struct cpu *c);
/* ----- */
void plat_interrupts_set_priority_mask(uint8_t min_priority);
/* New: */ 
void plat_interrupts_set_priority(uint32_t id, uint32_t core_pos,
				  uint32_t priority);
void plat_interrupts_enable(uint32_t id, uint32_t core_pos);
void plat_interrupts_disable(uint32_t id, uint32_t core_pos);
void plat_interrupts_set_type(uint32_t id, uint32_t type);
uint32_t plat_interrupts_get_type(uint32_t id);
uint32_t plat_interrupts_get_pending_interrupt_id(void);
void plat_interrupts_end_of_interrupt(uint32_t id);
void plat_interrupts_configure_interrupt(struct interrupt_descriptor int_desc);
void plat_interrupts_send_sgi(uint32_t id, bool send_to_all,
			      uint32_t target_list,
			      bool to_this_security_state);
/* ----- */

