/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "pg/vm_ids.h"

#define PG_VM_ID_BASE 0

/**
 * The ID of the primary VM, which is responsible for scheduling.
 *
 * This is not equal to its index because ID 0 is reserved for the hypervisor
 * itself. The primary VM therefore gets ID 1 and all other VMs come after that.
 */
#define PG_PRIMARY_VM_ID (PG_VM_ID_OFFSET + PG_PRIMARY_VM_INDEX)
