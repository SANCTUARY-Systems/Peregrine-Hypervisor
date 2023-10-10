/* SPDX-FileCopyrightText: 2019 The Hafnium Authors.     */
/* SPDX-FileCopyrightText: 2023 SANCTUARY Systems GmbH.  */
/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only */

#include "pg/plat/iommu.h"

bool plat_iommu_init(const struct fdt *fdt,
		     struct mm_stage1_locked stage1_locked, struct mpool *ppool)
{
	(void)fdt;
	(void)stage1_locked;
	(void)ppool;

	return true;
}

bool plat_iommu_unmap_iommus(struct vm_locked vm_locked, struct mpool *ppool)
{
	(void)vm_locked;
	(void)ppool;

	return true;
}

void plat_iommu_identity_map(struct vm_locked vm_locked, paddr_t begin,
			     paddr_t end, uint32_t mode)
{
	(void)vm_locked;
	(void)begin;
	(void)end;
	(void)mode;
}

bool plat_iommu_attach_peripheral(struct mm_stage1_locked stage1_locked,
				  struct vm_locked vm_locked,
				  const struct manifest_vm *manifest_vm,
				  struct mpool *ppool)
{
	(void)stage1_locked;
	(void)vm_locked;
	(void)manifest_vm;
	(void)ppool;

	return true;
}
