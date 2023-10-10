/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#pragma once

#include <span>
#include <vector>

namespace pma_test
{
std::vector<std::span<pte_t, MM_PTE_PER_PAGE>> get_ptable(
	const struct mm_ptable &ptable);

} /* namespace pma_test */
