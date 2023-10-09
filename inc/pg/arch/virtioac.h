#pragma once

#include "pg/std.h"
#include "pg/vcpu.h"
#include "pg/mm.h"

#define VIRTIO_START 0x1C130000
#define VIRTIO_END   0x1C13FFFF

bool virtioac_handle(uintreg_t esr, uintreg_t far, uint8_t pc_inc, struct vcpu *vcpu, struct vcpu_fault_info *info);
