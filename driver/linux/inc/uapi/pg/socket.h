/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#pragma once

#include <linux/socket.h>

#include <pg/ffa.h>

/* TODO: Reusing AF_ECONET for now as it's otherwise unused. */
#define AF_HF AF_ECONET
#define PF_HF AF_HF

/*
 * Address of a Hafnium socket
 */
struct pg_sockaddr {
	__kernel_sa_family_t family;
	ffa_vm_id_t vm_id;
	uint64_t port;
};
