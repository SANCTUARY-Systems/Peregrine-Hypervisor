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
 *     Copyright 2021 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

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
