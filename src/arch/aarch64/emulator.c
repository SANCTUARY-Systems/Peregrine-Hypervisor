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

#include "pg/arch/emulator.h"
#include "pg/arch/addr_translator.h"
#include "msr.h"
#include "sysregs.h"
#include "pg/dlog.h"
#include "pg/manifest.h"

struct interrupt_owner interrupts[MAX_INTERRUPTS];
struct spinlock spinlock = SPINLOCK_INIT;

static inline uint32_t io_read32(paddr_t addr)
{
	return *((uint32_t*)pa_addr(addr));
}

static inline void io_write32(paddr_t addr, uint32_t val)
{
	*((uint32_t*)pa_addr(addr)) = val;
}

static void rwp_wait()
{
	uint32_t max_wait = 1 << 20; //try 1M times

	while((io_read32(pa_init(FB_GICD_CTLR))) & FB_GICD_CTLR_RWP)
	{
		max_wait--;
		if(!max_wait)
		{
			dlog_error("Wait for FB_GICD_CTLR:RWP failed. Continuing anyway...");
			return;
		}
	}
}

void print_reg_name(uintreg_t addr)
{
    uint32_t cpu = 0;

#ifdef GICR_ENABLED
    if(addr >= FB_GICR && addr < FB_GICR + FB_GICR_SIZE)
    {
        /* using the proper way to determine the CPU-ID. for details, see  *
         * `gicr_adjust_cpu_offset()`.                                     *
         *                                                                 *
         * addr is translated to the appropriate offset in GICR frame 0 to *
         * make it CPU-ID agnostic.                                        */
        cpu   = (addr - FB_GICR) / FB_GICR_FRAME_SIZE;
        addr -= cpu * FB_GICR_FRAME_SIZE;
}
#endif /* GICR_ENABLED */

#ifdef GICD_ENABLED
    if(addr == FB_GICD_CTLR) {dlog("GICD_CTLR");}
    else if(addr == FB_GICD_TYPER) {dlog("GICD_TYPER");}
    else if(addr == FB_GICD_IIDR) {dlog("GICD_IIDR");}
    else if(addr == FB_GICD_TYPER2) {dlog("GICD_TYPER2");}
    else if(addr == FB_GICD_STATUSR) {dlog("GICD_STATUSR");}
    else if(addr == FB_GICD_SETSPI_NSR) {dlog("GICD_SETSPI_NSR");}
    else if(addr == FB_GICD_CLRSPI_NSR) {dlog("GICD_CLRSPI_NSR");}
    else if(addr == FB_GICD_SETSPI_SR) {dlog("GICD_SETSPI_SR");}
    else if(addr == FB_GICD_CLRSPI_SR) {dlog("GICD_CLRSPI_SR");}
    else if(addr >= FB_GICD_IGROUPR0 && addr <= (FB_GICD_IGROUPR0+(31*4))) {dlog("GICD_IGROUPR%d", ((addr-FB_GICD_IGROUPR0)/4));}
    else if(addr >= FB_GICD_ISENABLER0 && addr <= (FB_GICD_ISENABLER0+(31*4))) {dlog("GICD_ISENABLER%d", ((addr-FB_GICD_ISENABLER0)/4));}
    else if(addr >= FB_GICD_ICENABLER0 && addr <= (FB_GICD_ICENABLER0+(31*4))) {dlog("GICD_ICENABLER%d", ((addr-FB_GICD_ICENABLER0)/4));}
    else if(addr >= FB_GICD_ISPENDR0 && addr <= (FB_GICD_ISPENDR0+(31*4))) {dlog("GICD_ISPENDR%d", ((addr-FB_GICD_ISPENDR0)/4));}
    else if(addr >= FB_GICD_ICPENDR0 && addr <= (FB_GICD_ICPENDR0+(31*4))) {dlog("GICD_ICPENDR%d", ((addr-FB_GICD_ICPENDR0)/4));}
    else if(addr >= FB_GICD_ISACTIVER0 && addr <= (FB_GICD_ISACTIVER0+(31*4))) {dlog("GICD_ISACTIVER%d", ((addr-FB_GICD_ISACTIVER0)/4));}
    else if(addr >= FB_GICD_ICACTIVER0 && addr <= (FB_GICD_ICACTIVER0+(31*4))) {dlog("GICD_ICACTIVER%d", ((addr-FB_GICD_ICACTIVER0)/4));}
    else if(addr >= FB_GICD_IPRIORITYR0 && addr <= (FB_GICD_IPRIORITYR0+(254*4))) {dlog("GICD_IPRIORITYR%d", ((addr-FB_GICD_IPRIORITYR0)/4));}
    else if(addr >= FB_GICD_ITARGETSR0 && addr <= (FB_GICD_ITARGETSR0+(254*4))) {dlog("GICD_ITARGETSR%d", ((addr-FB_GICD_ITARGETSR0)/4));}
    else if(addr >= FB_GICD_ICFGR0 && addr <= (FB_GICD_ICFGR0+(63*4))) {dlog("GICD_ICFGR%d", ((addr-FB_GICD_ICFGR0)/4));}
    else if(addr >= FB_GICD_IGRPMODR0 && addr <= (FB_GICD_IGRPMODR0+(31*4))) {dlog("GICD_IGRPMODR%d", ((addr-FB_GICD_IGRPMODR0)/4));}
    else if(addr >= FB_GICD_NSACR0 && addr <= (FB_GICD_NSACR0+(63*4))) {dlog("GICD_NSACR%d", ((addr-FB_GICD_NSACR0)/4));}
    else if(addr == FB_GICD_SGIR) {dlog("GICD_SGIR");}
    else if(addr >= FB_GICD_CPENDSGIR0 && addr <= (FB_GICD_CPENDSGIR0+(3*4))) {dlog("GICD_CPENDSGIR%d", ((addr-FB_GICD_CPENDSGIR0)/4));}
    else if(addr >= FB_GICD_SPENDSGIR0 && addr <= (FB_GICD_SPENDSGIR0+(3*4))) {dlog("GICD_SPENDSGIR%d", ((addr-FB_GICD_SPENDSGIR0)/4));}
    else if(addr >= FB_GICD_INMIR0 && addr <= (FB_GICD_INMIR0+(31*4))) {dlog("GICD_INMIR%d", ((addr-FB_GICD_INMIR0)/4));}
    else if(addr >= FB_GICD_IGROUPR0E && addr <= (FB_GICD_IGROUPR0E+(31*4))) {dlog("GICD_IGROUPR%dE", (addr-FB_GICD_IGROUPR0E)/4);}
    else if(addr >= FB_GICD_ISENABLER0E && addr <= (FB_GICD_ISENABLER0E+(31*4))) {dlog("GICD_ISENABLER%dE", ((addr-FB_GICD_ISENABLER0E)/4));}
    else if(addr >= FB_GICD_ICENABLER0E && addr <= (FB_GICD_ICENABLER0E+(31*4))) {dlog("GICD_ICENABLER%dE", ((addr-FB_GICD_ICENABLER0E)/4));}
    else if(addr >= FB_GICD_ISPENDR0E && addr <= (FB_GICD_ISPENDR0E+(31*4))) {dlog("GICD_ISPENDR%dE", ((addr-FB_GICD_ISPENDR0E)/4));}
    else if(addr >= FB_GICD_ICPENDR0E && addr <= (FB_GICD_ICPENDR0E+(31*4))) {dlog("GICD_ICPENDR%dE", ((addr-FB_GICD_ICPENDR0E)/4));}
    else if(addr >= FB_GICD_ISACTIVER0E && addr <= (FB_GICD_ISACTIVER0E+(31*4))) {dlog("GICD_ISACTIVER%dE", ((addr-FB_GICD_ISACTIVER0E)/4));}
    else if(addr >= FB_GICD_ICACTIVER0E && addr <= (FB_GICD_ICACTIVER0E+(31*4))) {dlog("GICD_ICACTIVER%dE", ((addr-FB_GICD_ICACTIVER0E)/4));}
    else if(addr >= FB_GICD_IPRIORITYR0E && addr <= (FB_GICD_IPRIORITYR0E+(255*4))) {dlog("GICD_IPRIORITYR%dE", ((addr-FB_GICD_IPRIORITYR0E)/4));}
    else if(addr >= FB_GICD_ICFGR0E && addr <= (FB_GICD_ICFGR0E+(63*4))) {dlog("GICD_ICFGR%dE", ((addr-FB_GICD_ICFGR0E)/4));}
    else if(addr >= FB_GICD_IGRPMODR0E && addr <= (FB_GICD_IGRPMODR0E+(31*4))) {dlog("GICD_IGRPMODR%dE", ((addr-FB_GICD_IGRPMODR0E)/4));}
    else if(addr >= FB_GICD_NSACR0E && addr <= (FB_GICD_NSACR0E+(63*4))) {dlog("GICD_NSACR%dE", ((addr-FB_GICD_NSACR0E)/4));}
    else if(addr >= FB_GICD_INMIR0E && addr <= (FB_GICD_INMIR0E+(31*4))) {dlog("GICD_INMIR%dE", ((addr-FB_GICD_INMIR0E)/4));}
    else if(addr >= FB_GICD_IROUTER0 && addr <= (FB_GICD_IROUTER0+(988*8))) {dlog("GICD_IROUTER%d", ((addr-FB_GICD_IROUTER0)/8));}
    else if(addr >= FB_GICD_IROUTER0E && addr <= (FB_GICD_IROUTER0E+(1023*8))) {dlog("GICD_IROUTER%dE", ((addr-FB_GICD_IROUTER0E)/8));}
    else if(addr == FB_GICD_PIDR2) {dlog("GICD_PIDR2");}
#endif /* GICD_ENABLED */

#ifdef GITS_ENABLED
    else if(addr == FB_GITS_CTLR) {dlog("GITS_CTLR");}
    else if(addr == FB_GITS_IIDR) {dlog("GITS_IIDR");}
    else if(addr == FB_GITS_TYPER) {dlog("GITS_TYPER");}
    else if(addr == FB_GITS_MPAMIDR) {dlog("GITS_MPAMIDR");}
    else if(addr == FB_GITS_PARTIDR) {dlog("GITS_PARTIDR");}
    else if(addr == FB_GITS_MPIDR) {dlog("GITS_MPIDR");}
    else if(addr == FB_GITS_STATUSR) {dlog("GITS_STATUSR");}
    else if(addr == FB_GITS_UMSIR) {dlog("GITS_UMSIR");}
    else if(addr == FB_GITS_CBASER) {dlog("GITS_CBASER");}
    else if(addr == FB_GITS_CWRITER) {dlog("GITS_CWRITER");}
    else if(addr == FB_GITS_CREADR) {dlog("GITS_CREADR");}
    else if(addr == FB_GITS_BASER0) {dlog("GITS_BASER0");}
    else if(addr == FB_GITS_BASER1) {dlog("GITS_BASER1");}
    else if(addr == FB_GITS_BASER2) {dlog("GITS_BASER2");}
    else if(addr == FB_GITS_BASER3) {dlog("GITS_BASER3");}
    else if(addr == FB_GITS_BASER4) {dlog("GITS_BASER4");}
    else if(addr == FB_GITS_BASER5) {dlog("GITS_BASER5");}
    else if(addr == FB_GITS_BASER6) {dlog("GITS_BASER6");}
    else if(addr == FB_GITS_BASER7) {dlog("GITS_BASER7");}
    else if(addr == FB_GITS_SGIR) {dlog("GITS_SGIR");}
#endif /* GITS_ENABLED */

#ifdef GICR_ENABLED
    else if(addr == FB_GICR_CTLR) 			{dlog("(CPU%d) GICR_CTLR", cpu);}
    else if(addr == FB_GICR_IIDR) 			{dlog("(CPU%d) GICR_IIDR", cpu);}
    else if(addr == FB_GICR_TYPER) 		{dlog("(CPU%d) GICR_TYPER", cpu);}
    else if(addr == FB_GICR_STATUSR) 		{dlog("(CPU%d) GICR_STATUSR", cpu);}
    else if(addr == FB_GICR_WAKER) 		{dlog("(CPU%d) GICR_WAKER", cpu);}
    else if(addr == FB_GICR_MPAMIDR) 		{dlog("(CPU%d) GICR_MPAMIDR", cpu);}
    else if(addr == FB_GICR_PARTIDR) 		{dlog("(CPU%d) GICR_PARTIDR", cpu);}
    else if(addr == FB_GICR_SETLPIR) 		{dlog("(CPU%d) GICR_SETLPIR", cpu);}
    else if(addr == FB_GICR_CLRLPIR) 		{dlog("(CPU%d) GICR_CLRLPIR", cpu);}
    else if(addr == FB_GICR_PROPBASER) 	{dlog("(CPU%d) GICR_PROPBASER", cpu);}
    else if(addr == FB_GICR_PENDBASER) 	{dlog("(CPU%d) GICR_PENDBASER", cpu);}
    else if(addr == FB_GICR_INVLPIR) 		{dlog("(CPU%d) GICR_INVLPIR", cpu);}
    else if(addr == FB_GICR_INVALLR) 		{dlog("(CPU%d) GICR_INVALLR", cpu);}
    else if(addr == FB_GICR_SYNCR) 		{dlog("(CPU%d) GICR_SYNCR", cpu);}
    else if(addr == FB_GICR_IGROUPR0) 		{dlog("(CPU%d) GICR_IGROUPR0", cpu);}
    else if(addr == FB_GICR_IGROUPR1E) 	{dlog("(CPU%d) GICR_IGROUPR1E", cpu);}
    else if(addr == FB_GICR_IGROUPR2E) 	{dlog("(CPU%d) GICR_IGROUPR2E", cpu);}
    else if(addr == FB_GICR_ISENABLER0) 	{dlog("(CPU%d) GICR_ISENABLER0", cpu);}
    else if(addr == FB_GICR_ISENABLER1E) 	{dlog("(CPU%d) GICR_ISENABLER1E", cpu);}
    else if(addr == FB_GICR_ISENABLER2E) 	{dlog("(CPU%d) GICR_ISENABLER2E", cpu);}
    else if(addr == FB_GICR_ICENABLER0) 	{dlog("(CPU%d) GICR_ICENABLER0", cpu);}
    else if(addr == FB_GICR_ICENABLER1E) 	{dlog("(CPU%d) GICR_ICENABLER1E", cpu);}
    else if(addr == FB_GICR_ICENABLER2E) 	{dlog("(CPU%d) GICR_ICENABLER2E", cpu);}
    else if(addr == FB_GICR_ISPENDR0) 		{dlog("(CPU%d) GICR_ISPENDR0", cpu);}
    else if(addr == FB_GICR_ISPENDR1E) 	{dlog("(CPU%d) GICR_ISPENDR1E", cpu);}
    else if(addr == FB_GICR_ISPENDR2E) 	{dlog("(CPU%d) GICR_ISPENDR2E", cpu);}
    else if(addr == FB_GICR_ICPENDR0) 		{dlog("(CPU%d) GICR_ICPENDR0", cpu);}
    else if(addr == FB_GICR_ICPENDR1E) 	{dlog("(CPU%d) GICR_ICPENDR1E", cpu);}
    else if(addr == FB_GICR_ICPENDR2E) 	{dlog("(CPU%d) GICR_ICPENDR2E", cpu);}
    else if(addr == FB_GICR_ISACTIVER0) 	{dlog("(CPU%d) GICR_ISACTIVER0", cpu);}
    else if(addr == FB_GICR_ISACTIVER1E) 	{dlog("(CPU%d) GICR_ISACTIVER1E", cpu);}
    else if(addr == FB_GICR_ISACTIVER2E) 	{dlog("(CPU%d) GICR_ISACTIVER2E", cpu);}
    else if(addr == FB_GICR_ICACTIVER0) 	{dlog("(CPU%d) GICR_ICACTIVER0", cpu);}
    else if(addr == FB_GICR_ICACTIVER1E) 	{dlog("(CPU%d) GICR_ICACTIVER1E", cpu);}
    else if(addr == FB_GICR_ICACTIVER2E) 	{dlog("(CPU%d) GICR_ICACTIVER2E", cpu);}
    else if(addr >= FB_GICR_IPRIORITYR0 && addr <= (FB_GICR_IPRIORITYR0+(7*4))) {dlog("(CPU%d) GICR_IPRIORITYR%d", cpu, ((addr-FB_GICR_IPRIORITYR0)/4));}
    else if(addr >= FB_GICR_IPRIORITYR8E && addr <= (FB_GICR_IPRIORITYR8E+(16*4))) {dlog("(CPU%d) GICR_IPRIORITYR%dE", cpu, ((addr-FB_GICR_IPRIORITYR8E)/4)+8);}
    else if(addr == FB_GICR_ICFGR0) 		{dlog("(CPU%d) GICR_ICFGR0", cpu);}
    else if(addr == FB_GICR_ICFGR1E) 		{dlog("(CPU%d) GICR_ICFGR1E", cpu);}
    else if(addr == FB_GICR_ICFGR2E) 		{dlog("(CPU%d) GICR_ICFGR2E", cpu);}
    else if(addr == FB_GICR_ICFGR1) 		{dlog("(CPU%d) GICR_ICFGR1", cpu);}
    else if(addr == FB_GICR_IGRPMODR0) 	{dlog("(CPU%d) GICR_IGRPMODR0", cpu);}
    else if(addr == FB_GICR_IGRPMODR1E) 	{dlog("(CPU%d) GICR_IGRPMODR1E", cpu);}
    else if(addr == FB_GICR_IGRPMODR2E) 	{dlog("(CPU%d) GICR_IGRPMODR2E", cpu);}
    else if(addr == FB_GICR_NSACR) 		{dlog("(CPU%d) GICR_NSACR", cpu);}
    else if(addr == FB_GICR_INMIR0) 		{dlog("(CPU%d) GICR_INMIR0", cpu);}
    else if(addr == FB_GICR_INMIR1E) 		{dlog("(CPU%d) GICR_INMIR1E", cpu);}
    else if(addr == FB_GICR_INMIR2E) 		{dlog("(CPU%d) GICR_INMIR2E", cpu);}
    else if(addr == FB_GICR_PIDR2) 		{dlog("(CPU%d) GICR_PIDR2", cpu);}
#endif /* GICR_ENABLED */

    else {dlog("unknown register");}
}

/* TODO: the number of cores per cluster is configurable for Cortex-A at *
 *       design time, between 1-4. this should probably be configured at *
 *       Peregrine build time                                              */
#define CPUS_PER_CLUSTER 4U  /* CPU cores per cluster */
#define CLUSTERS_PER_SOC 2U  /* clusters per SoC      */

/* mpidr_to_no - converts MPIDR affinity levels to CPU number
 *  @data : CPU number expressed in MPIDR format (via affinity levels)
 *          must contain Multi-Processor Extensions if available
 *
 *  @return : CPU id (0 ... N-1)
 *
 * NOTE: if mpidr.MT == 1, Aff0 = Thread ID
 *                         Aff1 = CPU ID
 *                         Aff2 = Cluster ID
 *       else, Aff0 = CPU ID
 *             Aff1 = Cluster ID
 * NOTE: if mpidr.U == 1, this is a single core system
 * NOTE: assuming Aff3 is not implemented on our CPUs
 *
 * For details see:
 *  - Arm Cortex-A53 MPCore Processor Technical Reference Manual
 *  - Learn the architecture - Arm Generic Interrupt Controller v3 and v4
 *  - https://lwn.net/Articles/449167/
 */
static uint64_t
mpidr_to_no(uint64_t data)
{
    mpidr_t *mpidr = (mpidr_t *) &data;

    /* bypass Multi-Processor Extensions checks if not available */
    if (!mpidr->MPEA) {
        goto no_mpea;
    }

    /* single-core system */
    if (mpidr->U) {
        return 0;
    }

    /* multi-core system w/ hardware threading */
    if (mpidr->MT) {
        return mpidr->Aff2 * CPUS_PER_CLUSTER + mpidr->Aff1;
    }

no_mpea:
    /* multi-core system w/o hardware threading */
    return mpidr->Aff1 * CPUS_PER_CLUSTER + mpidr->Aff0;
}

/* aff_to_no - MP-aware wrapper over mpidr_to_no()
 *  @data : CPU number expressed in MPIDR format
 *
 *  @return : CPU id (0 ... N-1)
 *
 * When the Linux kernel performs a PSCI CPU_ON request, it specifies the
 * target CPU in MPIDR format, but only includes the affinity levels. Without
 * the Multi-Processor Extensions fields, we don't know what each affinity
 * level means.
 *
 * It's necessary to supply this information ourselves, based on the actual
 * value of the MPIDR register.
 *
 * E.g.: on the Toradex Verdin (i.MX8M Plus)
 *          MPIDR=0x80000001
 *              [31] = 1 -> Multi-Processor Extensions Available
 *              [24] = 0 -> Not Multi-Threaded -> Aff0 = CPU number
 *       on the Fixed Virtual Platform
 *          MPIDR=0x81000001
 *              [31] = 1 -> Multi-Processor Extensions Available
 *              [24] = 1 -> Multi-Threaded -> Aff0 = Thread number
 *                                            Aff1 = CPU number
 */
uint64_t
aff_to_no(uint64_t data)
{
    mpidr_t ref_mpidr = { 0 };

    /* get reference (true) MPIDR value */
    __asm__ volatile ("mrs %[dreg], MPIDR_EL1"
                     : [dreg] "=r" (ref_mpidr));

    /* make sure Multi-Processor Extension fields are set */
    return mpidr_to_no(data | (ref_mpidr.raw & 0xc1000000));
}

/*
 * Return "packed" affinity values, i.e.,
 * bit[0:7] = aff0, bit[15:8] = aff1, bit[23:16] = aff2
 *
 * NOTE: this implementation is not guaranteed to work on all CPUs.
 *       see aff_to_no() for reason why. since it's not used anywhere at the
 *       moment, I won't change it, but maybe we should consult MPIDR.U and
 *       MPIDR.MT before setting affinity values.
 */
uint32_t no_to_aff(uint32_t cpu_number)
{
	uint32_t aff2 = (cpu_number / CPUS_PER_CLUSTER);
	uint32_t aff1 = cpu_number - (aff2 * CPUS_PER_CLUSTER);
	return aff2 << 16 | aff1 << 8;
}

bool routed_to_vm(uint32_t intid, struct vm* vm)
{
	// check if interrupt routing is active
	if(!(*((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) & 0x80000000))
	{
		return false;
	}

	// not considering Aff3
	uint64_t target_id = *((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) & 0x7FFFFFFF;

	for(uint32_t i = 0; i < MAX_CPUS; i++)
	{
		if(vm->vcpus[i].cpu != NULL && vm->vcpus[i].state != VCPU_STATE_OFF && vm->vcpus[i].cpu->id == target_id)
		{
			return true;
		}
	}
	return false;
}

//reroute an interrupt to another physical CPU of a VM
//this might be necessary if the current CPU is assigned to another VM
/*
 * intid: interrupt ID for which the rerouting should be done
 * cpuid: ID of the physical CPU that should be "freed" from the interrupt
 */
void reroute_intid_to_vm(uint32_t intid, uint32_t cpuid)
{
	struct vm* vm = NULL;
	uint32_t cpuid_next = cpuid;
	if(intid < MAX_INTERRUPTS)
	{
		vm = interrupts[intid].vm;
	}
	if(intid >= 32 && intid <= 988)
	{

		for(int i = 0; i < vm->vcpu_count; i++)
		{
			if(vm->vcpus[i].state != VCPU_STATE_OFF && vm->vcpus[i].cpu->id != cpuid)
			{
				cpuid_next = vm->vcpus[i].cpu->id;
				break;
			}
		}

		if(cpuid_next == cpuid) //haven't found another CPU belonging to the VM
		{
			*((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) = 0;
			//TODO: Disable the interrupt or trap them in the hypervisor
		}
		else //found a new CPU where the interrupts should be routed to
		{
			*((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) =  (uint64_t)cpuid_next;
		}
	}
}

void reroute_all_interrupts(struct vm* vm, uint32_t cpuid)
{
	for(int i = 0; i < MAX_INTERRUPTS; i++)
	{
		if(interrupts[i].vm == vm)
		{
			reroute_intid_to_vm(i, cpuid);
		}
	}
}

//TODO: check that there are not interrupts active or pending before changing the routing
//set routing for the provided interrupt ID to the given CPU-ID
/*
 * intid: ID of the interrupt to be routed
 * cpuid: ID of the physical CPU the interrupt should be routed to
 * vm: pointer to the VM the interrupt should be routed to
 */
void route_intid_to_cpu(uint32_t intid, uint32_t cpuid, struct vm* vm)
{
	struct vm* old_vm = NULL;
	if(intid < MAX_INTERRUPTS)
	{
		old_vm = interrupts[intid].vm;
	}
	// first checking if the interrupt ID is one for which routing can be done
	if(intid >= 32 && intid <= 988)
	{
		if(*((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) != 0 && vm != old_vm)
		{
			reroute_intid_to_vm(intid, cpuid);
		}
		else
		{
			*((uint64_t*)(FB_GICD_IROUTER0+(((uint64_t)intid)*8))) = (uint64_t)cpuid; // | (0x1 << 31);
		}
		rwp_wait();
	}
}


void write_to_reg(struct vcpu* vcpu, uintreg_t addr, uintreg_t v_addr, uint8_t sas, uint64_t v_value)
{
	uint64_t value = v_value;
	if(addr >= FB_GICD_IROUTER0 && addr <= FB_GICD_IROUTER0E)
	{
		uint32_t host_id = 0;
		uint32_t target_no = aff_to_no(v_value);

		if(target_no < MAX_CPUS && vcpu->vm->vcpus[target_no].cpu != NULL && vcpu->vm->vcpus[target_no].state != VCPU_STATE_OFF)
		{
			host_id = vcpu->vm->vcpus[target_no].cpu->id;
		}
		else
		{
			/* If the "right" host_id of the routing target cannot be found use
			 * the physical on this the access was made, as this CPU belongs to the VMs
			 * that is calling and is active/online.
			 */
			host_id = vcpu->cpu->id;
		}
		value = (v_value & (1ul << 31)) | host_id;
	}

	if(addr == FB_GICD_CTLR)
	{
		value = *(uint32_t*)FB_GICD_CTLR; //don't allow VMs to (re)set the GIC
		v_value = v_value | 0x10; //affinity routing bit is fixed
	}

	//if GICR_ISENABLER -> update GICR_ICENABLER (and vice versa)

	//TODO: write the v_value to the vGIC and the adjusted value to the real GIC
	switch(sas)
	{
		case 0x0:
			*((uint8_t*)(addr)) = value; //write to real gic
			*((uint8_t*)(v_addr)) = v_value; //write to vgic
			break;
		case 0x1:
			*((uint16_t*)(addr)) = value; //write to real gic
			*((uint16_t*)(v_addr)) = v_value; //write to vgic
			break;
		case 0x2:
			*((uint32_t*)(addr)) = value; //write to real gic
			*((uint32_t*)(v_addr)) = v_value; //write to vgic
			break;
		case 0x3:
			*((uint64_t*)(addr)) = value; //write to real gic
			*((uint64_t*)(v_addr)) = v_value; //write to vgic
			break;
		default:
			dlog_error("write to GIC register error");
	}

	rwp_wait();


	// when enabling an interrupt the routing is configured to route it to the current physical CPU
	if(addr >= FB_GICD_ISENABLER0 && addr <= (FB_GICD_ISENABLER0+(31*4)))
	{
		uint32_t intid = (addr - FB_GICD_ISENABLER0)*8;
		for(uint32_t i = 0; i < 32; i++)
		{
			if(v_value & 0x1 && intid+i < MAX_INTERRUPTS)
			{
				if(!routed_to_vm(intid+i, vcpu->vm))
				{
					route_intid_to_cpu(intid+i, (read_msr(MPIDR_EL1)) & 0x700, vcpu->vm);
				}
				interrupts[intid+i].vm = vcpu->vm;
			}
			v_value = v_value >> 1;
		}
	}
}


/* gicr_adjust_cpu_offset - shifts an address into the corresponding GICR frame
 *  @addr : [in] accessed address (must be in the GICR range)
 *  @vm   : [in] VM that generated the GICR access
 *
 *  @return : the adjusted address
 */
uintpaddr_t
gicr_adjust_cpu_offset(uintpaddr_t addr, struct vm* vm)
{
    /* vCPU number is calculated as number of frames into the GICR (minus 1) *
     * the stride of a GICR frame is 128KB on GICv3 and 256KB on GICv4       *
     * NOTE: this is more reliable than just applying a mask                 */
    uint32_t pcpu_no = (read_msr(MPIDR_EL1) >> 8) & 0x7;
    uint32_t vcpu_no = (addr - FB_GICR) / FB_GICR_FRAME_SIZE;

    if(vcpu_no <= vm->vcpu_count)
    {
        pcpu_no = aff_to_no(vm->cpus[vcpu_no]);
    }

    /* shift address into the correct GICR frame                            *
     * each frame is by definition 128KB, hence the 0x20000                 *
     * NOTE: basically 1 frame per VM; how will it work with different *
     *       assignation models (i.e.: not sequentially allocating them)?   */
    addr += (uintpaddr_t) (pcpu_no - vcpu_no) * FB_GICR_FRAME_SIZE;

    return addr;
}

/* vgic_to_gic - translates vGIC accessed address to proper GIC equivalent
 *  @ipa : [in] accessed Intermediate Physical Address
 *  @vm  : [in] VM that generated the access
 *
 *  @return : adjusted address or NULL if outside supported device boundaries
 */
uintpaddr_t vgic_to_gic(uintpaddr_t ipa, struct vm* vm)
{
    uintpaddr_t corr_ipa;       /* corrected IPA */

    /* translate addresses from memory-bound registers to actual registers */
#ifdef GICD_ENABLED
    if (ipa >= (uintpaddr_t)vm->vgic->gicd
    &&  ipa <  (uintpaddr_t)vm->vgic->gicd+sizeof(vm->vgic->gicd))
    {
        return (ipa - (uintpaddr_t)vm->vgic->gicd) + FB_GICD;
    }
#endif /* GICD_ENABLED */
#ifdef GITS_ENABLED
    if (ipa >= (uintpaddr_t)vm->vgic->gits
    &&  ipa <  (uintpaddr_t)vm->vgic->gits+sizeof(vm->vgic->gits))
    {
        return (ipa - (uintpaddr_t)vm->vgic->gits) + FB_GITS;
    }
#endif /* GITS_ENABLED */
#ifdef GICR_ENABLED
    if (ipa >= (uintpaddr_t)vm->vgic->gicr
    &&  ipa  < (uintpaddr_t)vm->vgic->gicr+sizeof(vm->vgic->gicr))
    {
        corr_ipa = (ipa - (uintpaddr_t)vm->vgic->gicr) + FB_GICR;
        corr_ipa = gicr_adjust_cpu_offset(corr_ipa, vm);
        return corr_ipa;
    }
#endif /* GICR_ENABLED */

    return 0;
}

/**
 * this function manages the access to the GIC. Depending on the VM there
 * are three ways the input address will be read:
 * 1) VM has access to the GIC and the IPA in the info parameter matches the physical GIC address.
 * 2) VM uses a 1-to-1 mapped VGIC where the memory is not mapped to the VM in Stage 2, 
 *    then the VGIC address is also in the IPA of the info field.
 * 3) VM uses a different mapping and has the VGIC mapped in Stage 2 but without write permission,
 *    then the VGIC address can be generated by mapping the virtual address stored in FAR.
 */
bool
access_gicv3(uintreg_t              esr,
             uintreg_t              far,
             uint8_t                pc_inc,
             struct vcpu            *vcpu,
             struct vcpu_fault_info *info)
{
    bool        ret       = true;   /* function termination status */
    uintpaddr_t corr_addr = 0;      /* corrected IPA               */

	uintpaddr_t vgic_pa;
	uintpaddr_t far_pa = pa_addr(arch_translate_va_to_pa(va_init((uintvaddr_t) far), vcpu->vm->ptable));

    /* access to implemented vGIC components */
    if (info->ipaddr.ipa >= (uintpaddr_t) vcpu->vm->vgic
    &&  info->ipaddr.ipa <= (uintpaddr_t) vcpu->vm->vgic + sizeof(struct virt_gic))
    {
        corr_addr = vgic_to_gic(info->ipaddr.ipa, vcpu->vm);
		vgic_pa = info->ipaddr.ipa;
    } else if(far_pa >= (uintpaddr_t)vcpu->vm->vgic && far_pa <= (uintpaddr_t)vcpu->vm->vgic + sizeof(struct virt_gic))
	{
		corr_addr = vgic_to_gic(far_pa, vcpu->vm);
		vgic_pa = far_pa;
	}
    /* access to unimplemented vGIC components             *
     * perform passthrough if components physically exist  *
     * NOTE: remove these after integrating them into vGIC */
#ifdef GICC_ENABLED
    else if (info->ipaddr.ipa >= FB_GICC
         &&  info->ipaddr.ipa <  FB_GICC + FB_GICC_SIZE)
    {
        corr_addr = info->ipaddr.ipa;
		vgic_pa = info->ipaddr.ipa;
    }
#endif /* GICC_ENABLED */
#ifdef GICV_ENABLED
    else if (info->ipaddr.ipa >= FB_GICV
         &&  info->ipaddr.ipa <  FB_GICV + FB_GICV_SIZE)
    {
        corr_addr = info->ipaddr.ipa;
		vgic_pa = info->ipaddr.ipa;
    }
#endif /* GICV_ENABLED */
#ifdef GICH_ENABLED
    else if (info->ipaddr.ipa >= FB_GICH
         &&  info->ipaddr.ipa <  FB_GICH + FB_GICH_SIZE)
    {
        corr_addr = info->ipaddr.ipa;
		vgic_pa = info->ipaddr.ipa;
    }
#endif /* GICH_ENABLED */
    /* inject data abort fault into VM to solve it itself */
    else
    {
        ret = true;
        goto out;
    }

    if(!(esr & 0x1000000)) //Instruction Syndrome Valid (ISV)
    {
        ret = false;
        goto out;
    }

	/* if access is not a valid gic field */
	if (!corr_addr) {
		dlog_warning("Access is not a valid gic field. ipa:%#x far_pa:%#x\n", info->ipaddr.ipa, far_pa);
		ret = false;
		goto out;
	}

	sl_lock(&spinlock);

	//https://github.com/ARM-software/arm-trusted-firmware/blob/master/include/drivers/arm/gicv3.h
	if(info->mode == MM_MODE_R)
	{
		//dlog_debug("VM %x: ESR at read: %#x from %#x (%#x)\n", vcpu->vm, esr, info->ipaddr.ipa, info->vaddr);
		switch((esr >> 22) & 0x3)
		{
		case 0x0:
		case 0x1:
			break;
		case 0x2: //Word size opterion
			if(esr & 0x8000) //read word to 64bit register
			{
				#if LOG_LEVEL >= LOG_LEVEL_DEBUG
				dlog_debug("VM%d - PC: %#x", vcpu->vm->id, info->pc);
				dlog(" - reading from ");
				print_reg_name(corr_addr);
				dlog(" (%#x -> %#x) the value %#x into x[%d]\n", vgic_pa, info->vaddr, (*((uint32_t*)(vgic_pa)) & 0xFFFFFFFF), (esr >> 16) & 0x1F);
				#endif
				vcpu->regs.r[(esr >> 16) & 0x1F] = *((uint32_t*)(vgic_pa));
			}
			else //read word to 32bit register
			{
				#if LOG_LEVEL >= LOG_LEVEL_DEBUG
				dlog_debug("VM%d - PC: %#x", vcpu->vm->id, info->pc);
				dlog(" - reading from ");
				print_reg_name(corr_addr);
				dlog(" (%#x -> %#x) the value %#x into r[%d]\n", vgic_pa, info->vaddr, (*((uint64_t*)(vgic_pa)) & 0xFFFFFFFF), (esr >> 16) & 0x1F);
				#endif
				vcpu->regs.r[(esr >> 16) & 0x1F] = (vcpu->regs.r[(esr >> 16) & 0x1F] & 0xFFFFFFFF00000000)
						                         | (*((uint32_t*)(vgic_pa)) & 0x00000000FFFFFFFF);
			}
			break;
		case 0x3:
			if(esr & 0x8000) //read doubleword to 64bit register
			{
				#if LOG_LEVEL >= LOG_LEVEL_DEBUG
				dlog_debug("VM%d - PC: %#x", vcpu->vm->id, info->pc);
				dlog(" - reading from ");
				print_reg_name(corr_addr);
				dlog(" (%#x -> %#x) the value %#x into x[%d]\n", vgic_pa, info->vaddr, (*((uint32_t*)(vgic_pa))), (esr >> 16) & 0x1F);
				#endif
				vcpu->regs.r[(esr >> 16) & 0x1F] = *((uint64_t*)(vgic_pa));
			}
			else //read doubleword to 32bit register
			{
				#if LOG_LEVEL >= LOG_LEVEL_DEBUG
				dlog_debug("VM%d - PC: %#x", vcpu->vm->id, info->pc);
				dlog(" - reading from ");
				print_reg_name(corr_addr);
				dlog(" (%#x -> %#x) the value %#x into r[%d]\n", vgic_pa, info->vaddr, (*((uint64_t*)(vgic_pa))), (esr >> 16) & 0x1F);
				#endif
				vcpu->regs.r[(esr >> 16) & 0x1F] = (vcpu->regs.r[(esr >> 16) & 0x1F] & 0xFFFFFFFF00000000)
						                         | (*((uint64_t*)(vgic_pa)) & 0x00000000FFFFFFFF);
			}
			break;
		default:
			ret = false;
			goto out;
		}

		//__asm__ volatile("ldr w1, [x1, #0]" );
	}
	else if(info->mode == MM_MODE_W)
	{
//		//dlog_debug("VM %x: ESR at write: %#x (addr: %#x)\n", vcpu->vm, esr, info->ipaddr.ipa);
		if(((esr >> 16) & 0x1F) != 0x1F)
		{
			#if LOG_LEVEL >= LOG_LEVEL_DEBUG
			dlog_debug("VM%d - PC: %#x - writing from %c[%d] the value %#x into ", vcpu->vm->id, info->pc, ((esr >> 22) & 0x3) == 0x3 ? 'x' : 'r', (esr >> 16) & 0x1F, vcpu->regs.r[(esr >> 16) & 0x1F]);
			print_reg_name(corr_addr);
			dlog(" (%#x -> %#x)\n", vgic_pa, info->vaddr);
			#endif
			write_to_reg(vcpu, corr_addr, vgic_pa, (esr >> 22) & 0x3, vcpu->regs.r[(esr >> 16) & 0x1F]);
		}
		else
		{
			#if LOG_LEVEL >= LOG_LEVEL_DEBUG
			dlog_debug("VM%d - PC: %#x - writing 0 into ", vcpu->vm->id, info->pc);
			print_reg_name(corr_addr);
			dlog(" (%#x -> %#x)\n", vgic_pa, info->vaddr);
			#endif
			write_to_reg(vcpu, corr_addr, vgic_pa, (esr >> 22) & 0x3, 0);
		}
	}
	else
	{
		dlog_warning("ESR: %#x\n", esr);
		ret = false;
		goto out;
	}

out:
	vcpu->regs.pc += pc_inc;
	sl_unlock(&spinlock);

	return ret;
}


/**
 * Returns true if the ESR register shows an access to a ICC_* or ICV_*
 * register.
 */
bool icc_icv_is_register_access(uintreg_t esr)
{
	uintreg_t op0 = GET_ISS_OP0(esr);
	uintreg_t op1 = GET_ISS_OP1(esr);
	uintreg_t crn = GET_ISS_CRN(esr);
	uintreg_t crm = GET_ISS_CRM(esr);

	/* https://developer.arm.com/documentation/ddi0595/2021-06/Index-by-Encoding */
	return op0 == 0x3 && op1 == 0 && ((crn == 0xC && crm >= 0x8 && crm <= 0xC) || (crn == 0x4 && crm == 0x6));
}



#define ICC_PMR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0x4, 0x6, 0x0)
#define ICC_IAR0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x0)
#define ICC_EOIR0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x1)
#define ICC_HPPIR0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x2)
#define ICC_BPR0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x3)
#define ICC_AP0R0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x4)
#define ICC_AP0R1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x5)
#define ICC_AP0R2_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x6)
#define ICC_AP0R3_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x8, 0x7)
#define ICC_AP1R0_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x9, 0x0)
#define ICC_AP1R1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x9, 0x1)
#define ICC_AP1R2_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x9, 0x2)
#define ICC_AP1R3_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0x9, 0x3)
#define ICC_DIR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x1)
#define ICC_RPR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x3)
#define ICC_SGI1R_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x5)
#define ICC_ASGI1R_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x6)
#define ICC_SGI0R_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x7)
#define ICC_IAR1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x0)

#define ICC_EOIR1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x1)
#define ICC_HPPIR1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x2)
#define ICC_BPR1_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x3)
#define ICC_CTLR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x4)
#define ICC_SRE_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x5)
#define ICC_IGRPEN0_EL1_ENC 	GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x6)
#define ICC_IGRPEN1_EL1_ENC 	GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x7)

//#define ICC_SRE_EL2_ENC 		GET_ISS_ENCODING(0x3, 0x4, 0xC, 0x9, 0x5)
//#define ICC_CTLR_EL3_ENC 		GET_ISS_ENCODING(0x3, 0x5, 0xC, 0xC, 0x4)
//#define ICC_SRE_EL3_ENC 		GET_ISS_ENCODING(0x3, 0x5, 0xC, 0xC, 0x5)
//#define ICC_IGRPEN1_EL3_ENC 	GET_ISS_ENCODING(0x3, 0x5, 0xC, 0xC, 0x7)


/* ICV_* equal to ICC_* encoding
#define ICV_CTLR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xC, 0x4)
#define ICV_DIR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x1)
#define ICV_PMR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0x4, 0x6, 0x0)
#define ICV_RPR_EL1_ENC 		GET_ISS_ENCODING(0x3, 0x0, 0xC, 0xB, 0x3)
*/


/**
 * Processes an access (mrs) to a ICC_* or ICV_* register.
 * Returns true if the access was allowed and performed, false otherwise.
 */
bool icc_icv_process_access(struct vcpu *vcpu, uintreg_t esr)
{


	const struct vm *vm = vcpu->vm;
	uintreg_t sys_register = GET_ISS_SYSREG(esr);
	uintreg_t rt_register = GET_ISS_RT(esr);
	uintreg_t value;
	uintreg_t cpu_no = 0;

    uint32_t aff2;
    uint32_t aff1;
    uint32_t aff;

	/* +1 because Rt can access register XZR */
	CHECK(rt_register < NUM_GP_REGS + 1);

	if (ISS_IS_READ(esr))
	{
		switch(sys_register)
		{
		case ICC_PMR_EL1_ENC:
			value = read_msr(ICC_PMR_EL1);
			break;
		case ICC_IAR0_EL1_ENC:
			value = read_msr(ICC_IAR0_EL1);
			break;
//		case ICC_EOIR0_EL1_ENC:
//			value = read_msr(ICC_EOIR0_EL1);
//			break;
		case ICC_HPPIR0_EL1_ENC:
			value = read_msr(ICC_HPPIR0_EL1);
			break;
		case ICC_BPR0_EL1_ENC:
			value = read_msr(ICC_BPR0_EL1);
			break;
		case ICC_AP0R0_EL1_ENC:
			value = read_msr(ICC_AP0R0_EL1);
			break;
		case ICC_AP0R1_EL1_ENC:
			value = read_msr(ICC_AP0R1_EL1);
			break;
		case ICC_AP0R2_EL1_ENC:
			value = read_msr(ICC_AP0R2_EL1);
			break;
		case ICC_AP0R3_EL1_ENC:
			value = read_msr(ICC_AP0R3_EL1);
			break;
		case ICC_AP1R0_EL1_ENC:
			value = read_msr(ICC_AP1R0_EL1);
			break;
		case ICC_AP1R1_EL1_ENC:
			value = read_msr(ICC_AP1R1_EL1);
			break;
		case ICC_AP1R2_EL1_ENC:
			value = read_msr(ICC_AP1R2_EL1);
			break;
		case ICC_AP1R3_EL1_ENC:
			value = read_msr(ICC_AP1R3_EL1);
			break;
//		case ICC_DIR_EL1_ENC:
//			value = read_msr(ICC_DIR_EL1);
//			break;
		case ICC_RPR_EL1_ENC:
			value = read_msr(ICC_RPR_EL1);
			break;
//		case ICC_SGI1R_EL1_ENC:
//			value = read_msr(ICC_SGI1R_EL1);
//			break;
//		case ICC_ASGI1R_EL1_ENC:
//			value = read_msr(ICC_ASGI1R_EL1);
//			break;
//		case ICC_SGI0R_EL1_ENC:
//			value = read_msr(ICC_SGI0R_EL1);
//			break;
		case ICC_IAR1_EL1_ENC:
			value = read_msr(ICC_IAR1_EL1);
			break;
//		case ICC_EOIR1_EL1_ENC:
//			value = read_msr(ICC_EOIR1_EL1);
//			break;
		case ICC_HPPIR1_EL1_ENC:
			value = read_msr(ICC_HPPIR1_EL1);
			break;
		case ICC_BPR1_EL1_ENC:
			value = read_msr(ICC_BPR1_EL1);
			break;
		case ICC_CTLR_EL1_ENC:
			value = read_msr(ICC_CTLR_EL1);
			break;
		case ICC_SRE_EL1_ENC:
			value = read_msr(ICC_SRE_EL1);
			break;
		case ICC_IGRPEN0_EL1_ENC:
			value = read_msr(ICC_IGRPEN0_EL1);
			break;
		case ICC_IGRPEN1_EL1_ENC:
			value = read_msr(ICC_IGRPEN1_EL1);
			break;
//		case ICV_CTLR_EL1_ENC:
//			value = read_msr(ICV_CTLR_EL1);
//			break;
//		case ICV_DIR_EL1_ENC:
//			value = read_msr(ICV_DIR_EL1);
//			break;
//		case ICV_PMR_EL1_ENC:
//			value = read_msr(ICV_PMR_EL1);
//			break;
//		case ICV_RPR_EL1_ENC:
//			value = read_msr(ICV_RPR_EL1);
//			break;
		default:
			return false;
		}

		if (rt_register != RT_REG_XZR) {
			vcpu->regs.r[rt_register] = value;
		}

	}
	else
	{

		if (rt_register != RT_REG_XZR) {
			value = vcpu->regs.r[rt_register];
		} else {
			value = 0;
		}

		switch(sys_register)
		{
		case ICC_PMR_EL1_ENC:
			write_msr(ICC_PMR_EL1, value);
			break;
	//	case ICC_IAR0_EL1_ENC:
	//		write_msr(ICC_IAR0_EL1, value);
	//		break;
		case ICC_EOIR0_EL1_ENC:
			write_msr(ICC_EOIR0_EL1, value);
			break;
	//	case ICC_HPPIR0_EL1_ENC:
	//		write_msr(ICC_HPPIR0_EL1, value);
	//		break;
		case ICC_BPR0_EL1_ENC:
			write_msr(ICC_BPR0_EL1, value);
			break;
		case ICC_AP0R0_EL1_ENC:
			write_msr(ICC_AP0R0_EL1, value);
			break;
		case ICC_AP0R1_EL1_ENC:
			write_msr(ICC_AP0R1_EL1, value);
			break;
		case ICC_AP0R2_EL1_ENC:
			write_msr(ICC_AP0R2_EL1, value);
			break;
		case ICC_AP0R3_EL1_ENC:
			write_msr(ICC_AP0R3_EL1, value);
			break;
		case ICC_AP1R0_EL1_ENC:
			write_msr(ICC_AP1R0_EL1, value);
			break;
		case ICC_AP1R1_EL1_ENC:
			write_msr(ICC_AP1R1_EL1, value);
			break;
		case ICC_AP1R2_EL1_ENC:
			write_msr(ICC_AP1R2_EL1, value);
			break;
		case ICC_AP1R3_EL1_ENC:
			write_msr(ICC_AP1R3_EL1, value);
			break;
		case ICC_DIR_EL1_ENC:
			write_msr(ICC_DIR_EL1, value);
			break;
	//	case ICC_RPR_EL1_ENC:
	//		write_msr(ICC_RPR_EL1, value);
	//		break;
		case ICC_SGI1R_EL1_ENC:
            /* restructure ICC_SGI1R_EL1 Aff{2,1} fields into MPIDR *
             * format in order for aff_to_no() to work              */
            aff2 = (value >> 32) & 0xf;
            aff1 = (value >> 16) & 0xf;
            aff  = (aff2 << 16) | (aff1 << 8);

			cpu_no = aff_to_no(aff);
			if(cpu_no < vm->vcpu_count)
			{
				uint32_t pcpu = vm->cpus[cpu_no];
				value = value & 0x81000F000000; //keep all bits that are not affinity-related
				value |= (pcpu & 0xFF0000) << 16; //Aff2
				value |= (pcpu & 0xFF00) << 8; //Aff1
				value |= 0x1; //Aff0 always 1
			}
//			dlog_debug("VM%d writing %#016x to ICC_SGI1R_EL1 (Aff3: %02x, Aff2: %02x, Aff1: %02x), Targetlist: %04x\n",
//					vm->id, value, (value >> 48) & 0xf, (value >> 32) & 0xf, (value >> 16) & 0xf, value & 0xff);
			write_msr(ICC_SGI1R_EL1, value);
			break;
		case ICC_ASGI1R_EL1_ENC:
			write_msr(ICC_ASGI1R_EL1, value);
			break;
		case ICC_SGI0R_EL1_ENC:
			write_msr(ICC_SGI0R_EL1, value);
			break;
	//	case ICC_IAR1_EL1_ENC:
	//		write_msr(ICC_IAR1_EL1, value);
	//		break;
		case ICC_EOIR1_EL1_ENC:
			write_msr(ICC_EOIR1_EL1, value);
			break;
	//	case ICC_HPPIR1_EL1_ENC:
	//		write_msr(ICC_HPPIR1_EL1, value);
	//		break;
		case ICC_BPR1_EL1_ENC:
			write_msr(ICC_BPR1_EL1, value);
			break;
		case ICC_CTLR_EL1_ENC:
			write_msr(ICC_CTLR_EL1, value);
			break;
		case ICC_SRE_EL1_ENC:
			write_msr(ICC_SRE_EL1, value);
			break;
		case ICC_IGRPEN0_EL1_ENC:
			write_msr(ICC_IGRPEN0_EL1, value);
			break;
		case ICC_IGRPEN1_EL1_ENC:
			write_msr(ICC_IGRPEN1_EL1, value);
			break;
//		case ICV_CTLR_EL1_ENC:
//			write_msr(ICV_CTLR_EL1, value);
//			break;
//		case ICV_DIR_EL1_ENC:
//			write_msr(ICV_DIR_EL1, value);
//			break;
//		case ICV_PMR_EL1_ENC:
//			write_msr(ICV_PMR_EL1, value);
//			break;
//		case ICV_RPR_EL1_ENC:
//			write_msr(ICV_RPR_EL1, value);
//			break;
		default:
			return false;


		}
	}


	return true;
}

/**
 * @brief checks if instruction is cache maintenance operation.
 * For now only a subset of Instruction is supported.
 * TODO: This code is only temporary and cache maintenance should be rethought
 * in the future.
 * 
 * @param esr 
 * @return true if it is a cache maintenance operation
 * @return false else
 */
bool is_cache_maintenance(uintreg_t esr)
{
	uintreg_t op0 = GET_ISS_OP0(esr);
	uintreg_t op1 = GET_ISS_OP1(esr);
	uintreg_t crn = GET_ISS_CRN(esr);
	uintreg_t crm = GET_ISS_CRM(esr);

	/* https://developer.arm.com/documentation/ddi0595/2021-06/Index-by-Encoding?lang=en#dc_64 */
	return op0 == 0x1 && op1 == 0x0 && crn == 0x7 && (crm == 0x6 || crm == 0xA || crm == 0xE);
}

#define DC_IVAC_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x1)
#define DC_ISW_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x2)
#define DC_IGVAC_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x3)
#define DC_IGSW_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x4)
#define DC_IGDVAC_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x5)
#define DC_IGDSW_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0x6,0x6)
#define DC_CSW_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0xA,0x2)
#define DC_CGSW_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0xA,0x4)
#define DC_CGDSW_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0xA,0x6)
#define DC_CISW_ENC			GET_ISS_ENCODING(0x1,0x0,0x7,0xE,0x2)
#define DC_CIGSW_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0xE,0x4)
#define DC_CIGDSW_ENC		GET_ISS_ENCODING(0x1,0x0,0x7,0xE,0x6)

#define dc_ops(op, val)							\
({									\
	__asm__ volatile ("dc " op ", %0" :: "r" (val) : "memory");	\
})


bool process_cache_maintenance(struct vcpu *vcpu, uintreg_t esr)
{
	uintreg_t sys_register = GET_ISS_SYSREG(esr);
	uintreg_t rt_register = GET_ISS_RT(esr);
	uintreg_t value;

	if (rt_register != RT_REG_XZR) {
		value = vcpu->regs.r[rt_register];
	} else {
		value = 0;
	}

	switch (sys_register)
	{
	case DC_IVAC_ENC:
		dc_ops("IVAC",value);
		return true;
	case DC_ISW_ENC:
		dc_ops("ISW",value);
		return true;
	case DC_CSW_ENC:
		dc_ops("CSW",value);
		return true;
	case DC_CISW_ENC:
		dc_ops("CISW",value);
		return true;
#ifdef ARCH_FEAT_MTE2 // FIXME: this is not defined anywhere
	case DC_IGVAC_ENC:
		dc_ops("IGVAC",value);
		return true;
	case DC_IGSW_ENC:
		dc_ops("IGSW",value);
		return true;
	case DC_IGDVAC_ENC:
		dc_ops("IGDVAC",value);
		return true;
	case DC_IGDSW_ENC:
		dc_ops("IGDSW",value);
		return true;
	case DC_CGSW_ENC:
		dc_ops("CGSW",value);
		return true;
	case DC_CGDSW_ENC:
		dc_ops("CGDSW",value);
		return true;
	case DC_CIGSW_ENC:
		dc_ops("CIGSW",value);
		return true;
	case DC_CIGDSW_ENC:
		dc_ops("CIGDSW",value);
		return true;
#endif
	default:
		return false;
	}
}

void init_interrupt_owners()
{
	for(uint32_t i = 0; i < MAX_INTERRUPTS; i++)
	{
		interrupts[i].vm = NULL;
		interrupts[i].status = 0;
	}
}


void init_gic()
{
	paddr_t gicd_ctlr = pa_init(FB_GICD_CTLR);
	io_write32(gicd_ctlr, 0); //init FB_GICD_CTLR to zero
	io_write32(gicd_ctlr, io_read32(gicd_ctlr) | FB_GICD_CTLR_ARE_NS);
	rwp_wait();
	if(io_read32(gicd_ctlr) & FB_GICD_CTLR_ARE_NS)
	{
		io_write32(gicd_ctlr, io_read32(gicd_ctlr) | FB_GICD_CTLR_EnableGrp1A);
	}
	else
	{
		io_write32(gicd_ctlr, io_read32(gicd_ctlr) | FB_GICD_CTLR_EnableGrp1);;
	}
	rwp_wait();

	init_interrupt_owners();
}


/* init_vgic - initializes vGIC state
 *  @vm : VM for which the initialization is done
 *
 * TODO: check for values to be set in initial state
 */
void init_vgic(struct vm* vm)
{
#ifdef GICD_ENABLED
    memset_s(vm->vgic->gicd, sizeof(vm->vgic->gicd), 0, FB_GICD_SIZE);
    vm->vgic->gicd[FB_GICD_CTLR_OFFSET]                     = 0x00000010;
    vm->vgic->gicd[FB_GICD_TYPER_OFFSET / sizeof(uint32_t)] = 0x00780420;
    vm->vgic->gicd[FB_GICD_PIDR2_OFFSET / sizeof(uint32_t)] = 0x0000003B;
#endif /* GICD_ENABLED */

#ifdef GITS_ENABLED
    memset_s(vm->vgic->gits, sizeof(vm->vgic->gits), 0, FB_GITS_SIZE);
    vm->vgic->gits[FB_GITS_CTLR_OFFSET]                          = 0x80000000;
    vm->vgic->gits[FB_GITS_TYPER_OFFSET  / sizeof(uint32_t)]     = 0x000A0F7D;
    vm->vgic->gits[FB_GITS_BASER0_OFFSET / sizeof(uint32_t) + 1] = 0x01070000;
    vm->vgic->gits[FB_GITS_BASER2_OFFSET / sizeof(uint32_t) + 1] = 0x02070000;
    vm->vgic->gits[FB_GITS_BASER6_OFFSET / sizeof(uint32_t) + 1] = 0x04070000;
#endif /* GITS_ENABLED */

#ifdef GICR_ENABLED
    memset_s(vm->vgic->gicr, sizeof(vm->vgic->gicr), 0, FB_GICR_SIZE);
    for (uint32_t i = 0; i < vm->vcpu_count; i++)
    {
        vm->vgic->gicr[i][FB_GICR_PIDR2_OFFSET / sizeof(uint32_t)] = 0x3b;
        vm->vgic->gicr[i][FB_GICR_TYPER_OFFSET / sizeof(uint32_t)] =
            (i << 8) | 0x01;
        vm->vgic->gicr[i][FB_GICR_TYPER_OFFSET / sizeof(uint32_t) +1] =
            ((i / 4) << 16) | ((i % 4) << 8);
    }

    /* NOTE: last GICR_TYPER needs bit 4 set */
    vm->vgic->gicr[vm->vcpu_count - 1][FB_GICR_TYPER_OFFSET / sizeof(uint32_t)] |= 0x10;
#endif /* GICR_ENABLED */
}






