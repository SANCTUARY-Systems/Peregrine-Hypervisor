/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include "pg/panic.h"

/**
 * Only use to check assumptions which, if false, mean the system is in a bad
 * state and it is unsafe to continue.
 *
 * Do not use if the condition could ever be legitimately false e.g. when
 * processing external inputs.
 */
#define CHECK(x)                                                              \
	do {                                                                  \
		if (!(x)) {                                                   \
			panic("assertion failed (%s) at %s:%d", #x, __FILE__, \
			      __LINE__);                                      \
		}                                                             \
	} while (0)
