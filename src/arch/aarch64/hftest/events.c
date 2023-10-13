/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include "pg/arch/vm/events.h"

void event_wait(void)
{
	__asm__ volatile("wfe");
}

void event_send_local(void)
{
	__asm__ volatile("sevl");
}
