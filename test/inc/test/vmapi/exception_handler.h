/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include "vmapi/pg/ffa.h"

bool exception_handler_skip_instruction(void);

bool exception_handler_yield_unknown(void);

bool exception_handler_yield_data_abort(void);

bool exception_handler_yield_instruction_abort(void);

int exception_handler_get_num(void);

void exception_handler_reset(void);

void exception_handler_send_exception_count(void);

int exception_handler_receive_exception_count(
	const struct ffa_value *send_res,
	const struct ffa_memory_region *recv_buf);
