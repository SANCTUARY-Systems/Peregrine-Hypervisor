/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/fdt.h"
#include "pg/memiter.h"

#include "test/hftest_impl.h"

void hftest_use_registered_list(void);
void hftest_use_list(struct hftest_test list[], size_t count);

void hftest_json(void);
void hftest_run(struct memiter suite_name, struct memiter test_name,
		const struct fdt *fdt);
void hftest_help(void);
