/*
 * Copyright 2018 The Hafnium Authors.
 *
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/BSD-3-Clause.
 */

#pragma once

#include "pg/ffa.h"
#include "pg/types.h"

/* Keep macro alignment */
/* clang-format off */

#ifdef __ASSEMBLY__
#define _AC(X,Y)	X
#define _AT(T,X)	X
#else
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#endif

// #define BIT(pos, sfx)   (_AC(1, sfx) << (pos))

/* TODO: Define constants below according to spec. */
#define PG_MAILBOX_WRITABLE_GET        0xff01
#define PG_MAILBOX_WAITER_GET          0xff02
#define PG_INTERRUPT_ENABLE            0xff03
#define PG_INTERRUPT_GET               0xff04
#define PG_INTERRUPT_INJECT            0xff05

/* Custom FF-A-like calls returned from FFA_RUN. */
#define PG_FFA_RUN_WAIT_FOR_INTERRUPT 0xff06
#define PG_FFA_RUN_WAKE_UP            0xff07

/* This matches what Trusty and its TF-A module currently use. */
#define PG_DEBUG_LOG            0xbd000000


/* !PJ! SMC macros to make life easier */
#define SMCCC_VERSION_MAJOR_SHIFT            16
#define SMCCC_VERSION_MINOR_MASK             \
        ((1U << SMCCC_VERSION_MAJOR_SHIFT) - 1)
#define SMCCC_VERSION_MAJOR_MASK             ~SMCCC_VERSION_MINOR_MASK
#define SMCCC_VERSION_MAJOR(ver)             \
        (((ver) & SMCCC_VERSION_MAJOR_MASK) >> SMCCC_VERSION_MAJOR_SHIFT)
#define SMCCC_VERSION_MINOR(ver)             \
        ((ver) & SMCCC_VERSION_MINOR_MASK)

#define SMCCC_VERSION(major, minor)          \
    (((major) << SMCCC_VERSION_MAJOR_SHIFT) | (minor))

#define ARM_SMCCC_VERSION_1_0   SMCCC_VERSION(1, 0)
#define ARM_SMCCC_VERSION_1_1   SMCCC_VERSION(1, 1)

/*
 * This file provides common defines for ARM SMC Calling Convention as
 * specified in
 * http://infocenter.arm.com/help/topic/com.arm.doc.den0028a/index.html
 */

#define ARM_SMCCC_STD_CALL              _AC(0,U)
#define ARM_SMCCC_FAST_CALL             _AC(1,U)
#define ARM_SMCCC_TYPE_SHIFT            31

#define ARM_SMCCC_CONV_32               _AC(0,U)
#define ARM_SMCCC_CONV_64               _AC(1,U)
#define ARM_SMCCC_CONV_SHIFT            30

#define ARM_SMCCC_OWNER_MASK            _AC(0x3F,U)
#define ARM_SMCCC_OWNER_SHIFT           24

#define ARM_SMCCC_FUNC_MASK             _AC(0xFFFF,U)

/* List of known service owners */
#define ARM_SMCCC_OWNER_ARCH            0
#define ARM_SMCCC_OWNER_CPU             1
#define ARM_SMCCC_OWNER_SIP             2
#define ARM_SMCCC_OWNER_OEM             3
#define ARM_SMCCC_OWNER_STANDARD        4
#define ARM_SMCCC_OWNER_HYPERVISOR      5
#define ARM_SMCCC_OWNER_TRUSTED_APP     48
#define ARM_SMCCC_OWNER_TRUSTED_APP_END 49
#define ARM_SMCCC_OWNER_TRUSTED_OS      50
#define ARM_SMCCC_OWNER_TRUSTED_OS_END  63

#define ARM_SMCCC_OWNER_MASK            _AC(0x3F,U)
#define ARM_SMCCC_OWNER_SHIFT           24

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num)           \
        (((type) << ARM_SMCCC_TYPE_SHIFT) |                                     \
         ((calling_convention) << ARM_SMCCC_CONV_SHIFT) |                       \
         (((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) |          \
         (func_num))

/* Get service owner number from function identifier. */
static inline uint32_t smccc_get_owner(uint64_t funcid)
{
    return (funcid >> ARM_SMCCC_OWNER_SHIFT) & ARM_SMCCC_OWNER_MASK;
}


/* clang-format on */
