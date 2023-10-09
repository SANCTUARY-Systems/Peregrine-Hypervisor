/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#include "pg/arch/plat/ffa.h"

#include "pg/check.h"
#include "pg/ffa.h"
#include "pg/panic.h"
#include "pg/vm_ids.h"

#include "smc.h"

