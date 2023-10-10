/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/vm.h"

ffa_partition_properties_t arch_vm_partition_properties(ffa_vm_id_t id)
{
	(void)id;

	return 0;
}
