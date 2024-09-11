/*
 * Copyright (c) 2023 SANCTUARY Systems GmbH
 *
 * This file is free software: you may copy, redistribute and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * For a commercial license, please contact SANCTUARY Systems GmbH
 * directly at info@sanctuary.dev
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *     Copyright 2019 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include "debug_el1.h"

#include "pg/check.h"
#include "pg/dlog.h"
#include "pg/types.h"

#include "msr.h"
#include "sysregs.h"

/* clang-format off */

/**
 * Definitions of read-only debug registers' encodings.
 * See Arm Architecture Reference Manual Armv8-A, D12.2.
 * NAME, op0, op1, crn, crm, op2
 */
#define EL1_DEBUG_REGISTERS_READ	      \
	X(MDRAR_EL1	    , 2, 0, 1,  0, 0) \
	X(OSLSR_EL1	    , 2, 0, 1,  1, 4) \
	X(DBGAUTHSTATUS_EL1 , 2, 0, 7, 14, 6) \

/**
 * Definitions of write-only debug registers' encodings.
 * See Arm Architecture Reference Manual Armv8-A, D12.2.
 * NAME, op0, op1, crn, crm, op2
 */
#define EL1_DEBUG_REGISTERS_WRITE	      \
	X(OSLAR_EL1	    , 2, 0, 1,  0, 4) \

/**
 * Definitions of readable and writeable debug registers' encodings.
 * See Arm Architecture Reference Manual Armv8-A, D12.2.
 * NAME, op0, op1, crn, crm, op2
 */
#define EL1_DEBUG_REGISTERS_READ_WRITE        \
	X(OSDTRRX_EL1	    , 2, 0, 0,  0, 2) \
	X(MDCCINT_EL1	    , 2, 0, 0,  2, 0) \
	X(MDSCR_EL1	    , 2, 0, 0,  2, 2) \
	X(OSDTRTX_EL1	    , 2, 0, 0,  3, 2) \
	X(OSECCR_EL1	    , 2, 0, 0,  6, 2) \
	X(DBGBVR0_EL1	    , 2, 0, 0,  0, 4) \
	X(DBGBVR1_EL1	    , 2, 0, 0,  1, 4) \
	X(DBGBVR2_EL1	    , 2, 0, 0,  2, 4) \
	X(DBGBVR3_EL1	    , 2, 0, 0,  3, 4) \
	X(DBGBVR4_EL1	    , 2, 0, 0,  4, 4) \
	X(DBGBVR5_EL1	    , 2, 0, 0,  5, 4) \
	X(DBGBVR6_EL1	    , 2, 0, 0,  6, 4) \
	X(DBGBVR7_EL1	    , 2, 0, 0,  7, 4) \
	X(DBGBVR8_EL1	    , 2, 0, 0,  8, 4) \
	X(DBGBVR9_EL1	    , 2, 0, 0,  9, 4) \
	X(DBGBVR10_EL1	    , 2, 0, 0, 10, 4) \
	X(DBGBVR11_EL1	    , 2, 0, 0, 11, 4) \
	X(DBGBVR12_EL1	    , 2, 0, 0, 12, 4) \
	X(DBGBVR13_EL1	    , 2, 0, 0, 13, 4) \
	X(DBGBVR14_EL1	    , 2, 0, 0, 14, 4) \
	X(DBGBVR15_EL1	    , 2, 0, 0, 15, 4) \
	X(DBGBCR0_EL1       , 2, 0, 0,  0, 5) \
	X(DBGBCR1_EL1       , 2, 0, 0,  1, 5) \
	X(DBGBCR2_EL1       , 2, 0, 0,  2, 5) \
	X(DBGBCR3_EL1       , 2, 0, 0,  3, 5) \
	X(DBGBCR4_EL1       , 2, 0, 0,  4, 5) \
	X(DBGBCR5_EL1       , 2, 0, 0,  5, 5) \
	X(DBGBCR6_EL1       , 2, 0, 0,  6, 5) \
	X(DBGBCR7_EL1       , 2, 0, 0,  7, 5) \
	X(DBGBCR8_EL1       , 2, 0, 0,  8, 5) \
	X(DBGBCR9_EL1       , 2, 0, 0,  9, 5) \
	X(DBGBCR10_EL1      , 2, 0, 0, 10, 5) \
	X(DBGBCR11_EL1      , 2, 0, 0, 11, 5) \
	X(DBGBCR12_EL1      , 2, 0, 0, 12, 5) \
	X(DBGBCR13_EL1      , 2, 0, 0, 13, 5) \
	X(DBGBCR14_EL1      , 2, 0, 0, 14, 5) \
	X(DBGBCR15_EL1      , 2, 0, 0, 15, 5) \
	X(DBGWVR0_EL1       , 2, 0, 0,  0, 6) \
	X(DBGWVR1_EL1       , 2, 0, 0,  1, 6) \
	X(DBGWVR2_EL1       , 2, 0, 0,  2, 6) \
	X(DBGWVR3_EL1       , 2, 0, 0,  3, 6) \
	X(DBGWVR4_EL1       , 2, 0, 0,  4, 6) \
	X(DBGWVR5_EL1       , 2, 0, 0,  5, 6) \
	X(DBGWVR6_EL1       , 2, 0, 0,  6, 6) \
	X(DBGWVR7_EL1       , 2, 0, 0,  7, 6) \
	X(DBGWVR8_EL1       , 2, 0, 0,  8, 6) \
	X(DBGWVR9_EL1       , 2, 0, 0,  9, 6) \
	X(DBGWVR10_EL1      , 2, 0, 0, 10, 6) \
	X(DBGWVR11_EL1      , 2, 0, 0, 11, 6) \
	X(DBGWVR12_EL1      , 2, 0, 0, 12, 6) \
	X(DBGWVR13_EL1      , 2, 0, 0, 13, 6) \
	X(DBGWVR14_EL1      , 2, 0, 0, 14, 6) \
	X(DBGWVR15_EL1      , 2, 0, 0, 15, 6) \
	X(DBGWCR0_EL1       , 2, 0, 0,  0, 7) \
	X(DBGWCR1_EL1       , 2, 0, 0,  1, 7) \
	X(DBGWCR2_EL1       , 2, 0, 0,  2, 7) \
	X(DBGWCR3_EL1       , 2, 0, 0,  3, 7) \
	X(DBGWCR4_EL1       , 2, 0, 0,  4, 7) \
	X(DBGWCR5_EL1       , 2, 0, 0,  5, 7) \
	X(DBGWCR6_EL1       , 2, 0, 0,  6, 7) \
	X(DBGWCR7_EL1       , 2, 0, 0,  7, 7) \
	X(DBGWCR8_EL1       , 2, 0, 0,  8, 7) \
	X(DBGWCR9_EL1       , 2, 0, 0,  9, 7) \
	X(DBGWCR10_EL1      , 2, 0, 0, 10, 7) \
	X(DBGWCR11_EL1      , 2, 0, 0, 11, 7) \
	X(DBGWCR12_EL1      , 2, 0, 0, 12, 7) \
	X(DBGWCR13_EL1      , 2, 0, 0, 13, 7) \
	X(DBGWCR14_EL1      , 2, 0, 0, 14, 7) \
	X(DBGWCR15_EL1      , 2, 0, 0, 15, 7) \
	X(OSDLR_EL1         , 2, 0, 1,  3, 4) \
	X(DBGPRCR_EL1       , 2, 0, 1,  4, 4) \
	X(DBGCLAIMSET_EL1   , 2, 0, 7,  8, 6) \
	X(DBGCLAIMCLR_EL1   , 2, 0, 7,  9, 6)

/* clang-format on */

/**
 * Returns true if the ESR register shows an access to an EL1 debug register.
 */
bool debug_el1_is_register_access(uintreg_t esr)
{
	/*
	 * Architecture Reference Manual D12.2: op0 == 2 is for debug and trace
	 * system registers, op1 == 1 for trace, remaining are debug.
	 */
	return GET_ISS_OP0(esr) == 2 && GET_ISS_OP1(esr) != 1;
}

/**
 * Processes an access (msr, mrs) to an EL1 debug register.
 * Returns true if the access was allowed and performed, false otherwise.
 */
bool debug_el1_process_access(struct vcpu *vcpu, uint16_t vm_id,
			      uintreg_t esr)
{
	/*
	 * For now, debug registers are not supported by secondary VMs.
	 * Disallow accesses to them.
	 */
	if (vm_id != PG_PRIMARY_VM_ID) {
		//return false; //TODO: Handle access to system registers by multiple VMs properly
	}

	uintreg_t sys_register = GET_ISS_SYSREG(esr);
	uintreg_t rt_register = GET_ISS_RT(esr);
	uintreg_t value;

	/* +1 because Rt can access register XZR */
	CHECK(rt_register < NUM_GP_REGS + 1);

	if (ISS_IS_READ(esr)) {
		switch (sys_register) {
#define X(reg_name, op0, op1, crn, crm, op2)              \
	case (GET_ISS_ENCODING(op0, op1, crn, crm, op2)): \
		value = read_msr(reg_name);               \
		break;
			EL1_DEBUG_REGISTERS_READ
			EL1_DEBUG_REGISTERS_READ_WRITE
#undef X
		default:
			value = vcpu->regs.r[rt_register];
			dlog_notice(
				"Unsupported debug system register read: "
				"op0=%d, op1=%d, crn=%d, crm=%d, op2=%d, "
				"rt=%d.\n",
				GET_ISS_OP0(esr), GET_ISS_OP1(esr),
				GET_ISS_CRN(esr), GET_ISS_CRM(esr),
				GET_ISS_OP2(esr), GET_ISS_RT(esr));
			break;
		}
		if (rt_register != RT_REG_XZR) {
			vcpu->regs.r[rt_register] = value;
		}
	} else {
		if (rt_register != RT_REG_XZR) {
			value = vcpu->regs.r[rt_register];
		} else {
			value = 0;
		}
		switch (sys_register) {
#define X(reg_name, op0, op1, crn, crm, op2)              \
	case (GET_ISS_ENCODING(op0, op1, crn, crm, op2)): \
		write_msr(reg_name, value);               \
		break;
			EL1_DEBUG_REGISTERS_WRITE
			EL1_DEBUG_REGISTERS_READ_WRITE
#undef X
		default:
			dlog_notice(
				"Unsupported debug system register write: "
				"op0=%d, op1=%d, crn=%d, crm=%d, op2=%d, "
				"rt=%d.\n",
				GET_ISS_OP0(esr), GET_ISS_OP1(esr),
				GET_ISS_CRN(esr), GET_ISS_CRM(esr),
				GET_ISS_OP2(esr), GET_ISS_RT(esr));
			break;
		}
	}

	return true;
}
