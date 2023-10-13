/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

/**
 * Header for Hafnium messages
 *
 * NOTE: This is a work in progress.  The final form of a Hafnium message header
 * is likely to change.
 */
struct pg_msg_hdr {
	uint64_t src_port;
	uint64_t dst_port;
};
