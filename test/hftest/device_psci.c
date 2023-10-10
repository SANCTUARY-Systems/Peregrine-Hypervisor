/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/arch/vm/power_mgmt.h"

#include "test/hftest.h"

noreturn void hftest_device_reboot(void)
{
	arch_reboot();
}

void hftest_device_exit_test_environment(void)
{
	HFTEST_LOG("%s not supported", __func__);
}
