/*
 * Copyright 2021 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdatomic.h>

#include "pg/arch/types.h"

#include "vmapi/pg/ffa.h"

/**
 * Attributes encoding in the manifest:

 * Field		Bit(s)
 * ---------------------------
 * Priority		7:0
 * Security_State	8
 * Config(Edge/Level)	9
 * Type(SPI/PPI/SGI)	11:10
 * Reserved		31:12
 *
 * Implementation defined Encodings for various fields:
 *
 * Security_State:
 *	- Secure:	1
 *	- Non-secure:	0
 *
 * Configuration:
 *	- Edge triggered:	0
 *	- Level triggered:	1
 * Type:
 *	- SPI:	0b10
 *	- PPI:	0b01
 *	- SGI:	0b00
 *
 */

#define INT_DESC_TYPE_SPI 2
#define INT_DESC_TYPE_PPI 1
#define INT_DESC_TYPE_SGI 0

/** Interrupt Descriptor field masks and shifts. */

#define INT_DESC_PRIORITY_SHIFT 0
#define INT_DESC_SEC_STATE_SHIFT 8
#define INT_DESC_CONFIG_SHIFT 9
#define INT_DESC_TYPE_SHIFT 10

struct interrupt_descriptor {
	uint32_t interrupt_id;

	/**
	 * Bit fields	Position
	 * ---------------------
	 * reserved:	7:4
	 * type:	3:2
	 * config:	1
	 * sec_state:	0
	 */
	uint8_t type_config_sec_state;
	uint8_t priority;
	bool valid;
};

/**
 * Helper APIs for accessing interrupt_descriptor member variables.
 */
static inline uint32_t interrupt_desc_get_id(
	struct interrupt_descriptor int_desc)
{
	return int_desc.interrupt_id;
}

static inline uint8_t interrupt_desc_get_sec_state(
	struct interrupt_descriptor int_desc)
{
	return ((int_desc.type_config_sec_state >> 0) & 1U);
}

static inline uint8_t interrupt_desc_get_config(
	struct interrupt_descriptor int_desc)
{
	return ((int_desc.type_config_sec_state >> 1) & 1U);
}

static inline uint8_t interrupt_desc_get_type(
	struct interrupt_descriptor int_desc)
{
	return ((int_desc.type_config_sec_state >> 2) & 3U);
}

static inline uint8_t interrupt_desc_get_priority(
	struct interrupt_descriptor int_desc)
{
	return int_desc.priority;
}

static inline bool interrupt_desc_get_valid(
	struct interrupt_descriptor int_desc)
{
	return int_desc.valid;
}
