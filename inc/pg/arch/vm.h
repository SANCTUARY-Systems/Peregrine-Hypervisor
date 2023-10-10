/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include "pg/vm.h"

/**
 * Set architecture-specific features for the specified VM.
 */
void arch_vm_features_set(struct vm *vm);

/**
 * Return the FF-A partition info VM/SP properties given the VM id.
 */
ffa_partition_properties_t arch_vm_partition_properties(uint16_t id);
