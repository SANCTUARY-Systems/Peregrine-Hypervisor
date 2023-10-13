/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/vm/state.h"

#include "msr.h"

void per_cpu_ptr_set(uintptr_t v)
{
	write_msr(tpidr_el1, v);
}

uintptr_t per_cpu_ptr_get(void)
{
	return read_msr(tpidr_el1);
}
