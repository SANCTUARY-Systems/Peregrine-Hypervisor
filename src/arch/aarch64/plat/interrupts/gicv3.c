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
 *     Copyright 2021 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

/* New: */
#include "pg/cpu.h"
#include "pg/dlog.h"
#include "pg/interrupt_desc.h"
#include "pg/io.h"
#include "pg/panic.h"
#include "pg/plat/interrupts.h"
#include "pg/static_assert.h"
/* ---- */
#include "pg/types.h"

/* New: */
#include "gicv3_helpers.h"
/* ---- */
#include "msr.h"

 #define GICD_SIZE (0x10000)
 #define GICV3_REDIST_SIZE_PER_PE (0x20000) /* 128 KB */

 #define REDIST_LAST_FRAME_MASK (1 << 4)
 #define GICV3_REDIST_FRAMES_OFFSET GICV3_REDIST_SIZE_PER_PE

 struct gicv3_driver {
 	uintptr_t dist_base;
 	uintptr_t base_redist_frame;
 	uintptr_t all_redist_frames[MAX_CPUS];
 	struct spinlock lock;
 };

 static struct gicv3_driver plat_gicv3_driver;

 static uint32_t affinity_to_core_id(uint64_t reg)
 {
 	struct cpu *this_cpu;

 	this_cpu = cpu_find(reg & MPIDR_AFFINITY_MASK);

 	CHECK(this_cpu != NULL);

 	return cpu_index(this_cpu);
 }

 /**
  * This function checks the interrupt ID and returns true for SGIs and (E)PPIs
  * and false for (E)SPIs IDs.
  */
 static bool is_sgi_ppi(uint32_t id)
 {
 	/* SGIs: 0-15, PPIs: 16-31, EPPIs: 1056-1119. */
 	if (IS_SGI_PPI(id)) {
 		return true;
 	}

 	/* SPIs: 32-1019, ESPIs: 4096-5119. */
 	if (IS_SPI(id)) {
 		return false;
 	}

 	CHECK(false);
 	return false;
 }

 /**
  * This function returns the id of the highest priority pending interrupt at
  * the GIC cpu interface.
  */
 uint32_t gicv3_get_pending_interrupt_id(void)
 {
 	return (uint32_t)read_msr(ICC_IAR1_EL1) & IAR1_EL1_INTID_MASK;
 }

 /**
  * This function returns the type of the interrupt id depending on the group
  * this interrupt has been configured under by the interrupt controller i.e.
  * group1 Secure / group1 Non Secure. The return value can be one of the
  * following :
  *    INTR_GROUP1S : The interrupt type is a Secure Group 1 secure interrupt.
  *    INTR_GROUP1NS: The interrupt type is a Secure Group 1 non secure
  *                   interrupt.
  */
 uint32_t gicv3_get_interrupt_type(uint32_t id, uint32_t proc_num)
 {
 	uint32_t igroup;
 	uint32_t grpmodr;
 	uintptr_t gicr_base;

 	/* Ensure the parameters are valid. */
 	CHECK((id < PENDING_G1S_INTID) || (id >= MIN_LPI_ID));

 	/* All LPI interrupts are Group 1 non secure. */
 	if (id >= MIN_LPI_ID) {
 		return INTR_GROUP1NS;
 	}

 	/* Check interrupt ID. */
 	if (is_sgi_ppi(id)) {
 		/* SGIs: 0-15, PPIs: 16-31, EPPIs: 1056-1119. */
 		gicr_base = plat_gicv3_driver.all_redist_frames[proc_num];
 		igroup = gicr_get_igroupr(gicr_base, id);
 		grpmodr = gicr_get_igrpmodr(gicr_base, id);
 	} else {
 		/* SPIs: 32-1019, ESPIs: 4096-5119. */
 		igroup = gicd_get_igroupr(plat_gicv3_driver.dist_base, id);
 		grpmodr = gicd_get_igrpmodr(plat_gicv3_driver.dist_base, id);
 	}

 	/*
 	 * If the IGROUP bit is set, then it is a Group 1 Non secure
 	 * interrupt.
 	 */
 	if (igroup != 0U) {
 		return INTR_GROUP1NS;
 	}

 	/* If the GRPMOD bit is set, then it is a Group 1 Secure interrupt. */
 	if (grpmodr != 0U) {
 		return INTR_GROUP1S;
 	}

 	CHECK(false);
 }

 /**
  * This function enables the interrupt identified by id. The proc_num
  * is used if the interrupt is SGI or PPI, and programs the corresponding
  * Redistributor interface.
  */
 void gicv3_enable_interrupt(uint32_t id, uint32_t proc_num)
 {
 	CHECK(plat_gicv3_driver.dist_base != 0U);
 	CHECK(plat_gicv3_driver.base_redist_frame != 0U);

 	/*
 	 * Ensure that any shared variable updates depending on out of band
 	 * interrupt trigger are observed before enabling interrupt.
 	 */
 	dsb(ish);

 	/* Check interrupt ID. */
 	if (is_sgi_ppi(id)) {
 		/* For SGIs: 0-15, PPIs: 16-31 and EPPIs: 1056-1119. */
 		gicr_set_isenabler(
 			plat_gicv3_driver.all_redist_frames[proc_num], id);
 	} else {
 		/* For SPIs: 32-1019 and ESPIs: 4096-5119. */
 		gicd_set_isenabler(plat_gicv3_driver.dist_base, id);
 	}
 }

 /**
  * This function disables the interrupt identified by id. The proc_num
  * is used if the interrupt is SGI or PPI, and programs the corresponding
  * Redistributor interface.
  */
 void gicv3_disable_interrupt(uint32_t id, uint32_t proc_num)
 {
 	CHECK(plat_gicv3_driver.dist_base != 0U);
 	CHECK(plat_gicv3_driver.base_redist_frame != 0U);

 	/*
 	 * Disable interrupt, and ensure that any shared variable updates
 	 * depending on out of band interrupt trigger are observed afterwards.
 	 */

 	/* Check interrupt ID. */
 	if (is_sgi_ppi(id)) {
 		/* For SGIs: 0-15, PPIs: 16-31 and EPPIs: 1056-1119. */
 		gicr_set_icenabler(
 			plat_gicv3_driver.all_redist_frames[proc_num], id);

 		/* Write to clear enable requires waiting for pending writes. */
 		gicr_wait_for_pending_write(
 			plat_gicv3_driver.all_redist_frames[proc_num]);
 	} else {
 		/* For SPIs: 32-1019 and ESPIs: 4096-5119. */
 		gicd_set_icenabler(plat_gicv3_driver.dist_base, id);

 		/* Write to clear enable requires waiting for pending writes. */
 		gicd_wait_for_pending_write(plat_gicv3_driver.dist_base);
 	}

 	dsb(ish);
 }

 /**
  * This function sets the interrupt priority as supplied for the given interrupt
  * id.
  */
 void gicv3_set_interrupt_priority(uint32_t id, uint32_t core_pos,
 				  uint32_t priority)
 {
 	uintptr_t gicr_base;

 	/* Check interrupt ID. */
 	if (is_sgi_ppi(id)) {
 		if (core_pos >= MAX_CPUS) {
 			panic("Core index exceeds count of maximum cores\n");
 		}

 		/* For SGIs: 0-15, PPIs: 16-31 and EPPIs: 1056-1119. */
 		gicr_base = plat_gicv3_driver.all_redist_frames[core_pos];
 		gicr_set_ipriorityr(gicr_base, id, priority);
 	} else {
 		/* For SPIs: 32-1019 and ESPIs: 4096-5119. */
 		gicd_set_ipriorityr(plat_gicv3_driver.dist_base, id, priority);
 	}
 }

 /**
  * This function assigns group for the interrupt identified by id. The proc_num
  * is used if the interrupt is SGI or (E)PPI, and programs the corresponding
  * Redistributor interface. The group can be any of GICV3_INTR_GROUP*.
  */
 void gicv3_set_interrupt_type(uint32_t id, uint32_t proc_num, uint32_t type)
 {
 	bool igroup = false;
 	bool grpmod = false;
 	uintptr_t gicr_base;

 	CHECK(plat_gicv3_driver.dist_base != 0U);

 	switch (type) {
 	case INTR_GROUP1S:
 		igroup = false;
 		grpmod = true;
 		break;
 	case INTR_GROUP1NS:
 		igroup = true;
 		grpmod = false;
 		break;
 	default:
 		CHECK(false);
 		break;
 	}

 	/* Check interrupt ID. */
 	if (is_sgi_ppi(id)) {
 		/* For SGIs: 0-15, PPIs: 16-31 and EPPIs: 1056-1119. */
 		gicr_base = plat_gicv3_driver.all_redist_frames[proc_num];

 		igroup ? gicr_set_igroupr(gicr_base, id)
 		       : gicr_clr_igroupr(gicr_base, id);
 		grpmod ? gicr_set_igrpmodr(gicr_base, id)
 		       : gicr_clr_igrpmodr(gicr_base, id);
 	} else {
 		/* For SPIs: 32-1019 and ESPIs: 4096-5119. */

 		/* Serialize read-modify-write to Distributor registers. */
 		sl_lock(&plat_gicv3_driver.lock);

 		igroup ? gicd_set_igroupr(plat_gicv3_driver.dist_base, id)
 		       : gicd_clr_igroupr(plat_gicv3_driver.dist_base, id);
 		grpmod ? gicd_set_igrpmodr(plat_gicv3_driver.dist_base, id)
 		       : gicd_clr_igrpmodr(plat_gicv3_driver.dist_base, id);

 		sl_unlock(&plat_gicv3_driver.lock);
 	}
 }

 void gicv3_end_of_interrupt(uint32_t id)
 {
 	/*
 	 * Interrupt request deassertion from peripheral to GIC happens
 	 * by clearing interrupt condition by a write to the peripheral
 	 * register. It is desired that the write transfer is complete
 	 * before the core tries to change GIC state from 'AP/Active' to
 	 * a new state on seeing 'EOI write'.
 	 * Since ICC interface writes are not ordered against Device
 	 * memory writes, a barrier is required to ensure the ordering.
 	 * The dsb will also ensure *completion* of previous writes with
 	 * DEVICE nGnRnE attribute.
 	 */
 	dsb(ish);
 	write_msr(ICC_EOIR1_EL1, id);
 }

 uint64_t read_gicr_typer_reg(uintptr_t gicr_frame_addr)
 {
 	return io_read64(IO64_C(gicr_frame_addr + GICR_TYPER));
 }

 static inline uint32_t gicr_affinity_to_core_pos(uint64_t typer_reg)
 {
 	uint64_t aff3;
 	uint64_t aff2;
 	uint64_t aff1;
 	uint64_t aff0;
 	uint64_t reg;

 	aff3 = (typer_reg >> RDIST_AFF3_SHIFT) & (0xff);
 	aff2 = (typer_reg >> RDIST_AFF2_SHIFT) & (0xff);
 	aff1 = (typer_reg >> RDIST_AFF1_SHIFT) & (0xff);
 	aff0 = (typer_reg >> RDIST_AFF0_SHIFT) & (0xff);

 	/* Construct mpidr based on above affinities. */
 	reg = (aff3 << MPIDR_AFF3_SHIFT) | (aff2 << MPIDR_AFF2_SHIFT) |
 	      (aff1 << MPIDR_AFF1_SHIFT) | (aff0 << MPIDR_AFF0_SHIFT);

 	return affinity_to_core_id(reg);
 }

 static inline void populate_redist_base_addrs(void)
 {
 	uintptr_t current_rdist_frame;
 	uint64_t typer_reg;
 	uint32_t core_idx;

 	current_rdist_frame = plat_gicv3_driver.base_redist_frame;

 	while (true) {
 		typer_reg = read_gicr_typer_reg(current_rdist_frame);
 		core_idx = gicr_affinity_to_core_pos(typer_reg);

 		CHECK(core_idx < MAX_CPUS);
 		plat_gicv3_driver.all_redist_frames[core_idx] =
 			current_rdist_frame;

 		/* Check if this is the last frame. */
 		if (typer_reg & REDIST_LAST_FRAME_MASK) {
 			return;
 		}

 		current_rdist_frame += GICV3_REDIST_FRAMES_OFFSET;
 	}
 }

 static uint32_t find_core_pos(void)
 {
 	uint64_t mpidr_reg;

 	mpidr_reg = read_msr(MPIDR_EL1);

 	return affinity_to_core_id(mpidr_reg);
 }

 /**
  * Currently, TF-A has complete access to GIC driver and configures
  * GIC Distributor, GIC Re-distributor and CPU interfaces as needed.
  */
 void gicv3_distif_init(void)
 {
 	/* TODO: Currently, we skip this. */
 	return;

 	/* Enable G1S and G1NS interrupts. */
 	gicd_write_ctlr(
 		plat_gicv3_driver.dist_base,
 		CTLR_ENABLE_G1NS_BIT | CTLR_ENABLE_G1S_BIT | CTLR_ARE_S_BIT);
 }

 void gicv3_rdistif_init(uint32_t core_pos)
 {
 	/* TODO: Currently, we skip this. */
 	(void)core_pos;
 }

 void gicv3_cpuif_enable(uint32_t core_pos)
 {
 	/* TODO: Currently, we skip this. */
 	(void)core_pos;
 }

 void gicv3_send_sgi(uint32_t sgi_id, bool send_to_all, uint32_t target_list,
 		    bool to_this_security_state)
 {
 	uint64_t sgir;
 	uint64_t irm;
 	uint64_t mpidr_reg;

 	CHECK(is_sgi_ppi(sgi_id));

 	mpidr_reg = read_msr(MPIDR_EL1);
 	sgir = (sgi_id & SGIR_INTID_MASK) << SGIR_INTID_SHIFT;

 	/* Check the interrupt routing mode. */
 	if (send_to_all) {
 		irm = 1;
 	} else {
 		irm = 0;

 		/*
 		 * Find the affinity path of the PE for which SGI will be
 		 * generated.
 		 */

 		uint64_t aff1;
 		uint64_t aff2;
 		uint64_t aff3;

 		/*
 		 * Target List is a one hot encoding representing which cores
 		 * will be delivered the interrupt. At least one has to be
 		 * enabled.
 		 */

 		CHECK(target_list != 0U);

 		aff3 = (mpidr_reg >> MPIDR_AFF3_SHIFT) & (0xff);
 		aff2 = (mpidr_reg >> MPIDR_AFF2_SHIFT) & (0xff);
 		aff1 = (mpidr_reg >> MPIDR_AFF1_SHIFT) & (0xff);

 		/* Populate the various affinity fields. */
 		sgir |= ((aff3 & SGIR_AFF_MASK) << SGIR_AFF3_SHIFT) |
 			((aff2 & SGIR_AFF_MASK) << SGIR_AFF2_SHIFT) |
 			((aff1 & SGIR_AFF_MASK) << SGIR_AFF1_SHIFT);

 		/* Construct the SGI target affinity. */
 		sgir |= (target_list & SGIR_TGT_MASK) << SGIR_TGT_SHIFT;
 	}

 	/* Populate the Interrupt Routing Mode field. */
 	sgir |= (irm & SGIR_IRM_MASK) << SGIR_IRM_SHIFT;

 	if (to_this_security_state) {
 		write_msr(ICC_SGI1R_EL1, sgir);
 	} else {
 		write_msr(ICC_ASGI1R_EL1, sgir);
 	}

 	isb();
 }

 bool gicv3_driver_init(struct mm_stage1_locked stage1_locked,
 		       struct mpool *ppool)
 {
 	void *base_addr;

 	base_addr = mm_identity_map(stage1_locked, pa_init(GICD_BASE),
 				    pa_init(GICD_BASE + GICD_SIZE),
 				    MM_MODE_R | MM_MODE_W | MM_MODE_D, ppool);
 	if (base_addr == NULL) {
 		dlog_error("Could not map GICv3 into Peregrine memory map\n");
 		return false;
 	}

 	plat_gicv3_driver.dist_base = (uintptr_t)base_addr;

 	base_addr = mm_identity_map(
 		stage1_locked, pa_init(GICR_BASE),
 		pa_init(GICR_BASE + MAX_CPUS * GICV3_REDIST_SIZE_PER_PE),
 		MM_MODE_R | MM_MODE_W | MM_MODE_D, ppool);

 	if (base_addr == NULL) {
 		dlog_error("Could not map GICv3 into Peregrine memory map\n");
 		return false;
 	}

 	plat_gicv3_driver.base_redist_frame = (uintptr_t)base_addr;

 	populate_redist_base_addrs();

 	return true;
 }

 bool plat_interrupts_controller_driver_init(
 	const struct fdt *fdt, struct mm_stage1_locked stage1_locked,
 	struct mpool *ppool)
 {
 	(void)fdt;

 	if (!gicv3_driver_init(stage1_locked, ppool)) {
 		dlog_error("Failed to initialize GICv3 driver\n");
 		return false;
 	}

 	gicv3_distif_init();
 	gicv3_rdistif_init(find_core_pos());

 	return true;
 }

 void plat_interrupts_controller_hw_init(struct cpu *c)
 {
 	(void)c;
 	gicv3_cpuif_enable(find_core_pos());
 }
 
void plat_interrupts_set_priority_mask(uint8_t min_priority)
{
	write_msr(ICC_PMR_EL1, min_priority);
}
 /* New: */
 void plat_interrupts_set_priority(uint32_t id, uint32_t core_pos,
 				  uint32_t priority)
 {
 	gicv3_set_interrupt_priority(id, core_pos, priority);
 }

 void plat_interrupts_enable(uint32_t id, uint32_t core_pos)
 {
 	gicv3_enable_interrupt(id, core_pos);
 }

 void plat_interrupts_disable(uint32_t id, uint32_t core_pos)
 {
 	gicv3_disable_interrupt(id, core_pos);
 }

 void plat_interrupts_set_type(uint32_t id, uint32_t type)
 {
 	gicv3_set_interrupt_type(id, find_core_pos(), type);
 }

 uint32_t plat_interrupts_get_type(uint32_t id)
 {
 	return gicv3_get_interrupt_type(id, find_core_pos());
 }

 uint32_t plat_interrupts_get_pending_interrupt_id(void)
 {
 	return gicv3_get_pending_interrupt_id();
 }

 void plat_interrupts_end_of_interrupt(uint32_t id)
 {
 	gicv3_end_of_interrupt(id);
 }

 /**
  * Configure Group, priority, edge/level of the interrupt and enable it.
  */
 void plat_interrupts_configure_interrupt(struct interrupt_descriptor int_desc)
 {
 	uint32_t core_idx = find_core_pos();
 	uint32_t level_cfg = 0U;
 	uint32_t intr_num = interrupt_desc_get_id(int_desc);

 	/* Configure the interrupt as either G1S or G1NS. */
 	if (interrupt_desc_get_sec_state(int_desc) != 0) {
 		gicv3_set_interrupt_type(intr_num, core_idx, INTR_GROUP1S);
 	} else {
 		gicv3_set_interrupt_type(intr_num, core_idx, INTR_GROUP1NS);
 	}

 	/* Program the interrupt priority. */
 	gicv3_set_interrupt_priority(intr_num, core_idx,
 				     interrupt_desc_get_priority(int_desc));

 	if (interrupt_desc_get_config(int_desc) != 0) {
 		level_cfg = 1U;
 	}

 	/* Set interrupt configuration. */
 	if (is_sgi_ppi(intr_num)) {
 		/* GICR interface. */
 		gicr_set_icfgr(plat_gicv3_driver.all_redist_frames[core_idx],
 			       intr_num, level_cfg);
 	} else {
 		/* GICD interface. */
 		gicd_set_icfgr(plat_gicv3_driver.dist_base, intr_num,
 			       level_cfg);
 	}

 	/* Target SPI to primary CPU using affinity routing. */
 	if (IS_SPI(intr_num)) {
 		uint64_t gic_affinity_val;

 		gic_affinity_val =
 			gicd_irouter_val_from_mpidr(read_msr(MPIDR_EL1), 0U);
 		gicd_write_irouter(plat_gicv3_driver.dist_base, intr_num,
 				   gic_affinity_val);
 	}

 	/* Enable the interrupt now. */
 	gicv3_enable_interrupt(intr_num, core_idx);
 }

 void plat_interrupts_send_sgi(uint32_t id, bool send_to_all,
 			      uint32_t target_list, bool to_this_security_state)
 {
 	gicv3_send_sgi(id, send_to_all, target_list, to_this_security_state);
 }



