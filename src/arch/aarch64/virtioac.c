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
 */

#include "pg/arch/virtioac.h"
#include "pg/dlog.h"
#include "pg/panic.h"

static void writereg(struct vcpu *vcpu, uint8_t srt, uint8_t size, uint64_t data) {
	if        (size == 0) {
		data = (uint8_t ) data;
	} else if (size == 1) {
		data = (uint16_t) data;
	} else if (size == 2) {
		data = (uint32_t) data;
	} else if (size == 3) {
		data = (uint64_t) data;
	} else {panic("Unknown size");}

	if (srt == 31) { // XZR, so ignore it
		return; 

	} else if (srt <= 18 || srt == 29 || srt == 30) { // one of the saved registers
		vcpu->regs.r[srt] = data;
	
	} else // a register that was not saved
		   if (srt == 18) {
		__asm__ volatile("ldr x18, %0" : : "o" (data));
	} else if (srt == 19) {
		__asm__ volatile("ldr x19, %0" : : "o" (data));
	} else if (srt == 20) {
		__asm__ volatile("ldr x20, %0" : : "o" (data));
	} else if (srt == 21) {
		__asm__ volatile("ldr x21, %0" : : "o" (data));
	} else if (srt == 22) {
		__asm__ volatile("ldr x22, %0" : : "o" (data));
	} else if (srt == 23) {
		__asm__ volatile("ldr x23, %0" : : "o" (data));
	} else if (srt == 24) {
		__asm__ volatile("ldr x24, %0" : : "o" (data));
	} else if (srt == 25) {
		__asm__ volatile("ldr x25, %0" : : "o" (data));
	} else if (srt == 26) {
		__asm__ volatile("ldr x26, %0" : : "o" (data));
	} else if (srt == 27) {
		__asm__ volatile("ldr x27, %0" : : "o" (data));
	} else if (srt == 28) {
		__asm__ volatile("ldr x28, %0" : : "o" (data));

	} else {panic("Unknown register %s %d", __FUNCTION__, srt);}
}


static uint64_t readreg(struct vcpu *vcpu, uint8_t srt, uint8_t size) {
	uint64_t data;

	if (srt == 31) { // XZR
		data = 0; 

	} else if (srt <= 18 || srt == 29 || srt == 30) { // one of the saved registers
		data = vcpu->regs.r[srt];
	
	} else // a register that was not saved
		   if (srt == 18) {
		__asm__ volatile("str x18, %0" : "=m" (data) : );
	} else if (srt == 19) {
		__asm__ volatile("str x19, %0" : "=m" (data) : );
	} else if (srt == 20) {
		__asm__ volatile("str x20, %0" : "=m" (data) : );
	} else if (srt == 21) {
		__asm__ volatile("str x21, %0" : "=m" (data) : );
	} else if (srt == 22) {
		__asm__ volatile("str x22, %0" : "=m" (data) : );
	} else if (srt == 23) {
		__asm__ volatile("str x23, %0" : "=m" (data) : );
	} else if (srt == 24) {
		__asm__ volatile("str x24, %0" : "=m" (data) : );
	} else if (srt == 25) {
		__asm__ volatile("str x25, %0" : "=m" (data) : );
	} else if (srt == 26) {
		__asm__ volatile("str x26, %0" : "=m" (data) : );
	} else if (srt == 27) {
		__asm__ volatile("str x27, %0" : "=m" (data) : );
	} else if (srt == 28) {
		__asm__ volatile("str x28, %0" : "=m" (data) : );

	} else {panic("Unknown register %s %d", __FUNCTION__, srt);}

	if        (size == 0) {
		data = (uint8_t ) data;
	} else if (size == 1) {
		data = (uint16_t) data;
	} else if (size == 2) {
		data = (uint32_t) data;
	} else if (size == 3) {
		data = (uint64_t) data;
	} else {panic("Unknown size");}

	return data;
}






#define BITMASK(var, start, length) ((var)>>(start) & ((1U<<(length))-1))

#define EM(start, length) (BITMASK(esr, start, length))

bool virtioac_handle(uintreg_t esr, uintreg_t far, uint8_t pc_inc, struct vcpu *vcpu, struct vcpu_fault_info *info) {

	if ((info->ipaddr.ipa >= VIRTIO_START) && (info->ipaddr.ipa <= VIRTIO_END) &&
		(EM(26, 6) == 044) && //Data Abort from a lower Exception level
		(EM(25, 1) == 1) && //32-bit instruction trapped
		(EM(24, 1) == 1) && //Instruction Syndrome Valid
		//EM(22, 2): Syndrome Access Size. 0=8, 1=16, 2=32, 3=64
		(EM(21, 1) == 0) && //sign extension
		// EM(16, 5): Syndrome Register Transfer. The register number of the Wt/Xt/Rt operand of the faulting instruction
		(EM(15, 1) == 0) && //64-bit register identified by instruction
		(EM(14, 1) == 0) && //Instruction did have acquire/release semantics
		(EM(13, 1) == 0) && //Indicates that the fault came from use of VNCR_EL2
		(EM(12, 2) == 0) && //??
		(EM(10, 1) == 0) && //FAR not Valid
		(EM( 9, 1) == 0) && //External abort type
		(EM( 8, 1) == 0) && //Cache maintenance
		(EM( 7, 1) == 0) && //Fault on the stage 2 translation of an access for a stage 1 translation table walk
		// EM( 6, 1): WnR: Abort caused by an instruction writing to a memory location
		(EM( 0, 5) == 007) && //Data Fault Status Code == Translation fault, level 3
		true) {

		uint8_t srt = EM(16, 5); //Syndrome Register Transfer. The register number of the Wt/Xt/Rt operand of the faulting instruction
		uint64_t data;

		bool wnr = EM(6, 1); // 0=read operation, 1=write operation
		uint8_t size = EM(22, 2);

		if (!wnr) { // Read
			if        (size == 0) {
				data = *((uint8_t *) (info->ipaddr.ipa));
			} else if (size == 1) {
				data = *((uint16_t*) (info->ipaddr.ipa));
			} else if (size == 2) {
				data = *((uint32_t*) (info->ipaddr.ipa));
			} else if (size == 3) {
				data = *((uint64_t*) (info->ipaddr.ipa));
			} else {panic("Unknown size");}

			dlog_debug("%s: Read.  ESR: %#x from: %#x (%#x) to: %#x size: %d data: %#x\n", __FUNCTION__, esr, info->ipaddr.ipa, info->vaddr, srt, 8*(1<<size), data);

			writereg(vcpu, srt, size, data);

		} else { // Write
			data = readreg(vcpu, srt, size);

			dlog_debug("%s: Write. ESR: %#x to: %#x (%#x) from: %#x size: %d data: %#x\n", __FUNCTION__, esr, info->ipaddr.ipa, info->vaddr, srt, 8*(1<<size), data);

			if        (size == 0) {
				*((uint8_t* ) (info->ipaddr.ipa)) = (uint8_t ) data;
			} else if (size == 1) {
				*((uint16_t*) (info->ipaddr.ipa)) = (uint16_t) data;
			} else if (size == 2) {
				*((uint32_t*) (info->ipaddr.ipa)) = (uint32_t) data;
			} else if (size == 3) {
				*((uint64_t*) (info->ipaddr.ipa)) = (uint64_t) data;
			} else {panic("Unknown size");}
			
		}

		vcpu->regs.pc += pc_inc;
		return true;
	
	} else {
		dlog_warning("%s: Giving up. ESR: %#x from: %#x (%#x)\n", __FUNCTION__, esr, info->ipaddr.ipa, info->vaddr);

		return false;
	}
}
