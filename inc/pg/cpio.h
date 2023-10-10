/* SPDX-FileCopyrightText: 2018 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <stdbool.h>

#include "pg/memiter.h"
#include "pg/string.h"

bool cpio_get_file(const struct memiter *cpio, const struct string *name,
		   struct memiter *it);
