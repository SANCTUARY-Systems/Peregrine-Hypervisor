/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pg/arch/types.h"

#include "pg/addr.h"
#include "pg/vcpu.h"

#include "vmapi/pg/ffa.h"

/**
 * Reset the register values other than the PC and argument which are set with
 * `arch_regs_set_pc_arg()`.
 */
void arch_regs_reset(struct vcpu *vcpu);

/**
 * Updates the given registers so that when a vCPU runs, it starts off at the
 * given address (pc) with the given argument.
 *
 * This function must only be called on an arch_regs that is known not be in use
 * by any other physical CPU.
 */
void arch_regs_set_pc_arg(struct arch_regs *r, ipaddr_t pc, uintreg_t arg);

/**
 * Updates the register holding the return value of a function.
 *
 * This function must only be called on an arch_regs that is known not be in use
 * by any other physical CPU.
 */
void arch_regs_set_retval(struct arch_regs *r, struct ffa_value v);

/**
 * Extracts SMC or HVC arguments from the registers of a vCPU.
 *
 * This function must only be called on an arch_regs that is known not be in use
 * by any other physical CPU.
 */
struct ffa_value arch_regs_get_args(struct arch_regs *regs);

/**
 * Initialize and reset CPU-wide register values.
 */
void arch_cpu_init(struct cpu *c, ipaddr_t entry_point);
