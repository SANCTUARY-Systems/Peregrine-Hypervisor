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
 *     Copyright 2019 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/vm.h"

#include "hypervisor/feature_id.h"

void arch_vm_features_set(struct vm *vm)
{
	/* Features to trap for all VMs. */

	/*
	 * It is not safe to enable this yet, in part, because the feature's
	 * registers are not context switched in Hafnium.
	 */
	vm->arch.trapped_features |= PG_FEATURE_LOR;

	vm->arch.trapped_features |= PG_FEATURE_SPE;

	vm->arch.trapped_features |= PG_FEATURE_TRACE;

	vm->arch.trapped_features |= PG_FEATURE_DEBUG;

	if (vm->id != PG_PRIMARY_VM_ID) {
		/* Features to trap only for the secondary VMs. */

		vm->arch.trapped_features |= PG_FEATURE_PERFMON;

		/*
		 * TODO(b/132395845): Access to RAS registers is not trapped at
		 * the moment for the primary VM, only for the secondaries. RAS
		 * register access isn't needed now, but it might be
		 * required for debugging. When Hafnium introduces debug vs
		 * release builds, trap accesses for primary VMs in release
		 * builds, but do not trap them in debug builds.
		 */
		vm->arch.trapped_features |= PG_FEATURE_RAS;

		/*
		 * The PAuth mechanism holds state in the key registers. Only
		 * the primary VM is allowed to use the PAuth functionality for
		 * now. This prevents Hafnium from having to save/restore the
		 * key register on a VM switch.
		 */
		vm->arch.trapped_features |= PG_FEATURE_PAUTH;
	}
}

ffa_partition_properties_t arch_vm_partition_properties(uint16_t id)
{
	/*
	 * VMs supports indirect messaging.
	 * PVM supports sending direct messages.
	 * Secondary VMs support receiving direct messages.
	 */
	return FFA_PARTITION_INDIRECT_MSG | ((id == PG_PRIMARY_VM_ID)
		       ? FFA_PARTITION_DIRECT_SEND
		       : FFA_PARTITION_DIRECT_RECV);
}
