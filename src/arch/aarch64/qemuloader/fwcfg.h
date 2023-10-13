/* SPDX-FileCopyrightText: 2020 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdbool.h>
#include <stdint.h>

#define FW_CFG_ID 0x01
#define FW_CFG_KERNEL_SIZE 0x08
#define FW_CFG_INITRD_SIZE 0x0b
#define FW_CFG_KERNEL_DATA 0x11
#define FW_CFG_INITRD_DATA 0x12

uint32_t fw_cfg_read_uint32(uint16_t key);
void fw_cfg_read_bytes(uint16_t key, uintptr_t destination, uint32_t length);
bool fw_cfg_read_dma(uint16_t key, uintptr_t destination, uint32_t length);
