/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <stdint.h>

void per_cpu_ptr_set(uintptr_t v);
uintptr_t per_cpu_ptr_get(void);
