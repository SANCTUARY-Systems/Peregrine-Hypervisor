/* SPDX-FileCopyrightText: 2021 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */
#include "pg/plat/interrupts.h"
#include "pg/types.h"

/* New: */
bool plat_interrupts_controller_driver_init(
	const struct fdt *fdt, struct mm_stage1_locked stage1_locked,
	struct mpool *ppool)
{
	(void) fdt;
	(void)stage1_locked;
	(void)ppool;
	return true;	
}

void plat_interrupts_controller_hw_init(struct cpu *c)
{
	(void)c;
}
 /* ------- */
void plat_interrupts_set_priority_mask(uint8_t min_priority)
{
	(void)min_priority;
}

/* New: */
void plat_interrupts_set_priority(uint32_t id, uint32_t core_pos,
				  uint32_t priority)
{
	(void)id;
	(void)core_pos;
	(void)priority;
}

void plat_interrupts_disable(uint32_t id, uint32_t core_pos)
{
	(void)id;
	(void)core_pos;
}

void plat_interrupts_set_type(uint32_t id, uint32_t type)
{
	(void)id;
	(void)type;
}

uint32_t plat_interrupts_get_type(uint32_t id)
{
	(void)id;
	return 0;
}

uint32_t plat_interrupts_get_pending_interrupt_id(void)
{
	return 0;
}

void plat_interrupts_end_of_interrupt(uint32_t id)
{
	(void)id;
}

void plat_interrupts_configure_interrupt(struct interrupt_descriptor int_desc)
{
	(void)int_desc;
}

void plat_interrupts_send_sgi(uint32_t id, bool send_to_all,
			      uint32_t target_list, bool to_this_security_state)
{
	(void)id;
	(void)send_to_all;
	(void)target_list;
	(void)to_this_security_state;
}
