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
 *     Copyright 2018 The Hafnium Authors.
 *
 *     Use of this source code is governed by a BSD-style
 *     license that can be found in the LICENSE file or at
 *     https://opensource.org/licenses/BSD-3-Clause.
 */

#include <stdbool.h>

#include "smc.h"

#include "pg/arch/vm.h"
#include "pg/arch/tee/mediator.h"
#include "pg/arch/emulator.h"
#include "pg/arch/virt_devs.h"

#include "pg/manifest.h"
#include "pg/plat/console.h"
#include "pg/plat/iommu.h"
#include "pg/plat/interrupts.h"

#include "pg/load.h"
#include "pg/api.h"
#include "pg/boot_params.h"
#include "pg/check.h"
#include "pg/dlog.h"
#include "pg/layout.h"
#include "pg/memiter.h"
#include "pg/mm.h"
#include "pg/pma.h"
#include "pg/static_assert.h"
#include "pg/std.h"
#include "pg/vm.h"
#include "pg/panic.h"
#include "pg/error.h"

#include "vmapi/pg/call.h"

/******************************************************************************
 ************************* INTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* copy_to_unmapped - Copies data to an unmapped location
 *  @stage1_locked : Currently locked stage-1 page table of the HV
 *  @to            : Destination address
 *  @from_it       : Source address & size
 *  @ppool         : Memory pool
 *
 *  @return : true if everything went well; false otherwise
 *
 * Maps destination, copies data, then unmaps it again. The data is written so
 * that it is available to all cores with the cache disabled. When switching to
 * the partitions, the caching is initially disabled so the data must be
 * available without the cache.
 */
static bool
copy_to_unmapped(struct mm_stage1_locked stage1_locked,
                 paddr_t                 to,
                 struct memiter          *from_it,
                 struct mpool            *ppool)
{
    const void *from;    /* src address           */
    size_t      size;    /* copy size             */
    paddr_t     to_end;  /* dst end address       */
    void        *ptr;    /* HV mapped dst address */
    bool        ans;     /* answer                */

    from = memiter_base(from_it);
    size = memiter_size(from_it);
    to_end = pa_add(to, size);

    /* map destination buffer */
    ptr = mm_identity_map_and_reserve(
            stage1_locked, to, to_end, MM_MODE_W, ppool);
    RET(!ptr, false, "unable to map [%#x - %#x]\n", to, to_end);

    /* copy content */
    memcpy_s(ptr, size, from, size);
    arch_mm_flush_dcache(ptr, size);

    /* unmap destination buffer */
    ans = mm_unmap(stage1_locked, to, to_end, ppool);
    DIE(!ans, "unable to unmap [%#x - %#x]\n");

    return true;
}

/* copy_to_allocated - Copies data to already mapped location
 *  @to      : Destination address
 *  @from_it : Source address and size
 *
 *  @return : true
 *
 * Same cache related notes from `copy_to_unmapped()` apply here.
 */
static bool
copy_to_allocated(paddr_t to, struct memiter *from_it)
{
    const void *from;   /* src address */
    size_t      size;   /* copy size   */

    from = memiter_base(from_it);
    size = memiter_size(from_it);

    /* copy content */
    memcpy_s((void *)to.pa, size, from, size);
    arch_mm_flush_dcache((void *)to.pa, size);

    return true;
}

/* infer_interrupt - unpacks interrupt attribute into descriptor structure
 *  @interrupt : Interrupt number & attribute
 *
 *  @return : Descriptor needed to configure & enable interrupt
 */
static struct interrupt_descriptor
infer_interrupt(struct interrupt interrupt)
{
    struct interrupt_descriptor int_desc;   /* returned descriptor */
    uint32_t                    attr;       /* easy access         */

    /* extract interrupt id, priority, etc. from attributes */
    attr = interrupt.attributes;

    int_desc.interrupt_id = interrupt.id;
    int_desc.priority     = (attr >> INT_DESC_PRIORITY_SHIFT) & 0xff;
    int_desc.type_config_sec_state =
        (((attr >> INT_DESC_TYPE_SHIFT) & 0x3) << 2) |
        (((attr >> INT_DESC_CONFIG_SHIFT) & 0x1) << 1) |
        ((attr >> INT_DESC_SEC_STATE_SHIFT) & 0x1);
    int_desc.valid = true;

    return int_desc;
}

/******************************************************************************
 ************************* EXTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* print_manifest - Dumps relevant manifest information regarding specific VM
 *  @manifest_vm : Manifest data pertaining to the VM in question
 *  @vm_id       : VM's order in the system manifest
 *
 */
void
print_manifest(struct manifest_vm *manifest_vm,
               uint16_t           vm_id)
{
    dlog_debug("\n===================== %u =======================\n", vm_id);
    dlog_debug("debug_name: %s\n",      manifest_vm->debug_name);
    dlog_debug("kernel_filename: %s\n", manifest_vm->kernel_filename);
    dlog_debug("kernel_addr_pa: %p\n",  manifest_vm->kernel_addr_pa);
    dlog_debug("smc_whitlist\n");
    dlog_debug("  permissive: %s\n",
        manifest_vm->smc_whitelist.permissive ? "true" : "false");

    RET(manifest_vm->smc_whitelist.smc_count > MAX_SMCS, ,
        "VM %#x exceeded SMC whitelist quota", vm_id);

    for (size_t i = 0; i < manifest_vm->smc_whitelist.smc_count; i++)
        dlog_debug("  smc[%u]: %#x\n", i, manifest_vm->smc_whitelist.smcs[i]);

    dlog_debug("  dev_region_count: %u\n", manifest_vm->dev_region_count);
    for (size_t i = 0; i < manifest_vm->dev_region_count; i++) {
        dlog_debug("  dev_region %u\n", i);
        dlog_debug("    base_address: %#x\n",
            manifest_vm->dev_regions[i].base_address);
        dlog_debug("    page_count: %#x\n",
            manifest_vm->dev_regions[i].page_count);
        dlog_debug("    attributes: %#x\n",
            manifest_vm->dev_regions[i].attributes);

        for (size_t j = 0;
             j < manifest_vm->dev_regions[i].interrupt_count; 
             j++)
        {
            dlog_debug("    interrupt %u: id %u, attributes: %#x\n",
                j, manifest_vm->dev_regions[i].interrupts[j].id,
                manifest_vm->dev_regions[i].interrupts[j].attributes);
        }
    }

    dlog_debug("boot_address: %#x\n",     manifest_vm->boot_address);
    dlog_debug("ramdisk_filename: %#x\n", manifest_vm->ramdisk_filename);
    dlog_debug("ramdisk_addr_pa: %p\n",   manifest_vm->ramdisk_addr_pa);
    dlog_debug("fdt_filename: %s\n",      manifest_vm->fdt_filename);
    dlog_debug("fdt_addr_pa: %p\n",       manifest_vm->fdt_addr_pa);

    dlog_debug("memory_layout:\n");
    dlog_debug("  gic: %#x\n",     manifest_vm->mem_layout.gic);
    dlog_debug("  kernel: %#x\n",  manifest_vm->mem_layout.kernel);
    dlog_debug("  fdt: %#x\n",     manifest_vm->mem_layout.fdt);
    dlog_debug("  ramdisk: %#x\n", manifest_vm->mem_layout.ramdisk);

    dlog_debug("vcpu_count: %u\n", manifest_vm->vcpu_count);
    dlog_debug("===============================================\n\n");
}

/******************************************************************************
 ************************* VM COMPONENTS INITIALIZERS *************************
 ******************************************************************************/

/* load_fdt - Loads the VM's Flattened Device Tree
 *  @stage1_locked   : Currently locked stage-1 page table of the HV
 *  @begin           : FDT buffer start address (NULL if not yet allocated)
 *  @end             : FDT buffer end address (NULL if not yet allocated)
 *  @ipa_begin       : Guest physical address where FDT is mapped
 *  @manifest_vm     : Ptr to the VM manifest structure
 *  @cpio            : Ptr to the CPIO archive containing the FDT
 *  @ppool           : Memory pool
 *  @fdt_size        : Location where FDT size is stored (can be NULL)
 *  @boot_kernel_arg : FDT address passed to the kernel at boot
 *
 *  @return : true if everything went well; false otherwise
 */
static bool
load_fdt(struct mm_stage1_locked stage1_locked,
         paddr_t                 begin,
         paddr_t                 end,
         ipaddr_t                ipa_begin,
         struct manifest_vm      *manifest_vm,
         const struct memiter    *cpio,
         struct mpool            *ppool,
         size_t                  *fdt_size,
         uintreg_t               *boot_kernel_arg)
{
    struct memiter fdt;     /* FDT buffer */
    size_t         size;    /* FDT size   */
    void           *ans_p;  /* answer     */
    bool           ans;     /* answer     */

    /* determine size & location of FDT file in CPIO archive */
    ans = cpio_get_file(cpio, &manifest_vm->fdt_filename, &fdt);
    RET(!ans, false, "unable to find FDT file \"%s\"\n",
        string_data(&manifest_vm->fdt_filename));

    size = memiter_size(&fdt);
    if (fdt_size)
        *fdt_size = size;

    /* FDT buffer allocation & VM mapping handled by us */
    if (begin.pa == pa_init(0).pa || end.pa == pa_init(0).pa) {
        dlog_debug("allocating memory for FDT (size=%#x)\n", size);

        /* force buffer allocation to match FDT's IPA */
        if (manifest_vm->identity_mapping) {
            begin = pa_from_ipa(ipa_begin);
            end   = pa_from_ipa(ipa_add(ipa_begin, size));

            ans_p = mm_identity_map_and_reserve(
                        stage1_locked, begin, end, MM_MODE_R | MM_MODE_W, ppool);
            RET(!ans_p, false, "unable to create direct mapping: [%#x - %#x]\n",
                begin.pa, end.pa);
        } else {
            begin = pa_init(pma_alloc(stage1_locked.ptable, ipa_begin, size,
                                MM_MODE_R | MM_MODE_W, HYPERVISOR_ID, ppool));
        }

        RET(begin.pa == pma_get_fault_ptr(), false,
            "unable to allocate memory for VM's FDT\n");

        ans = copy_to_allocated(begin, &fdt);
        RET(!ans, false, "unable to copy FDT from CPIO\n");

        /* assign HV allocated buffer (containing FDT) to the VM              *
         * NOTE: unmapping the FDT buffer from the HV page table is premature *
         *       at this point. future accesses (probably from fdt_patch.c)   *
         *       cause a same-level read access exception. not sure that      *
         *       we're cleaning it up before starting the VM.                 */
        pma_assign(&manifest_vm->vm->ptable, begin.pa, ipa_begin,
                   pma_get_size(begin.pa, HYPERVISOR_ID), MM_MODE_R,
                   manifest_vm->vm->id, ppool);
        pma_free(stage1_locked.ptable, begin.pa, HYPERVISOR_ID, ppool);

        /* update manifest with newly allocated buffer info */
        manifest_vm->fdt_addr_pa = begin;
        manifest_vm->fdt_size    = size;
    }
    /* FDT buffer allocation & VM mapping already handled by caller */
    else {
        RET(pa_difference(begin, end) < size, false,
            "FDT larger than available memory\n");

        ans = copy_to_allocated(begin, &fdt);
        RET(!ans, false, "unable to copy FDT from CPIO\n");
    }

    /* if replacing an existing FDT, update kernel's boot parameters as well */
    if (pa_addr(manifest_vm->fdt_addr_pa) != *boot_kernel_arg) {
        dlog_debug("new fdt set in kernel_args: %#x\n", ipa_addr(ipa_begin));
        *boot_kernel_arg = ipa_addr(ipa_begin);
    }

    return true;
}

/* load_kernel - Loads the VM's kernel
 *  @stage1_locked : Currently locked stage-1 page table of the HV
 *  @begin         : kernel buffer start address (NULL if not yet allocated)
 *  @end           : kernel buffer end address (NULL if not yet allocated)
 *  @ipa_begin     : Guest physical address where kernel is mapped
 *  @manifest_vm   : Ptr to the VM manifest structure
 *  @cpio          : Ptr to the CPIO archive containing the kernel
 *  @ppool         : Memory pool
 *  @kernel_size   : Location where kernel size is stored (can be NULL)
 *
 *  @return : true if everything went well; false otherwise
 *
 * Details on the ARM kernel image header:
 *  1) https://www.kernel.org/doc/html/latest/arm64/booting.html
 *  2) http://weng-blog.com/2017/03/linux-kernel-image/
 */
static bool
load_kernel(struct mm_stage1_locked stage1_locked,
            paddr_t                 begin,
            paddr_t                 end,
            ipaddr_t                ipa_begin,
            struct manifest_vm      *manifest_vm,
            const struct memiter    *cpio,
            struct mpool            *ppool,
            size_t                  *kernel_size)
{
    const struct __attribute__((packed)) {
        uint32_t code0;       /* executable code                        */
        uint32_t code1;       /* executable code                        */
        uint64_t text_offset; /* image load offset, little endian       */
        uint64_t image_size;  /* effective image size, little endian    */
        uint64_t flags;       /* kernel flags, little endian            */
        uint64_t : 64;        /* reserved                               */
        uint64_t : 64;        /* reserved                               */
        uint64_t : 64;        /* reserved                               */
        uint32_t ih_magic;    /* 0x644d5241 magic number, little endian */
        uint32_t : 32;        /* reserved (used for PE COFF offset)     */
    } *hdrptr;      /* ARM kernel image header */

    struct memiter kernel;    /* kernel buffer                           */
    size_t         memsize;   /* kernel buffer size (may include header) */
    size_t         filesize;  /* kernel image size (w/o header)          */
    void           *ans_p;    /* answer                                  */
    bool           ans;       /* answer                                  */

    /* determine size & location of kernel file / image in CPIO archive */
    ans = cpio_get_file(cpio, &manifest_vm->kernel_filename, &kernel);
    RET(!ans, false, "unable to find kenrel file \"%s\"\n",
        string_data(&manifest_vm->kernel_filename));

    hdrptr   = (void *) memiter_base(&kernel);
    filesize = memiter_size(&kernel);
    memsize  = (hdrptr->ih_magic == 0x644d5241) ? hdrptr->image_size
                                                : filesize;
    if (kernel_size)
        *kernel_size = memsize;

    /* kernel buffer allocation & VM mapping handled by us */
    if (begin.pa == pa_init(0).pa || end.pa == pa_init(0).pa) {
        dlog_debug("allocating memory for kernel (filesize=%#x, memsize=%#x)\n",
                   filesize, memsize);

        /* NOTE: image must be placed at text_offset from a 2MB-aligned *
         *       base address; see [1] for details                      */

        /* force buffer allocation to match kernel's IPA */
        if (manifest_vm->identity_mapping) {
            begin = pa_from_ipa(ipa_begin);
            end   = pa_from_ipa(ipa_add(ipa_begin, memsize));

            ans_p = mm_identity_map_and_reserve(stage1_locked, begin, end,
                        MM_MODE_R | MM_MODE_W | MM_MODE_X, ppool);
            RET(!ans_p, false, "unable to create direct mapping: [%#x - %#x]\n",
                begin.pa, end.pa);
        } else {
            begin = pa_init(pma_aligned_alloc(
                                stage1_locked.ptable, ipa_begin,
                                memsize, PAGE_LEVEL_BITS,
                                MM_MODE_R | MM_MODE_W | MM_MODE_X,
                                HYPERVISOR_ID, ppool));
        }

        RET(begin.pa == pma_get_fault_ptr(), false,
            "unable to allocate memory for VM's kernel\n");

        ans = copy_to_allocated(begin, &kernel);
        RET(!ans, false, "unable to copy kernel from CPIO\n");

        /* reassign HV allocated buffer (containing kernel) to the VM */
        pma_assign(&manifest_vm->vm->ptable, begin.pa, ipa_begin,
                   pma_get_size(begin.pa, HYPERVISOR_ID),
                   MM_MODE_R | MM_MODE_W | MM_MODE_X,
                   manifest_vm->vm->id, ppool);
        pma_free(stage1_locked.ptable, begin.pa, HYPERVISOR_ID, ppool);

        /* update manifest with newly allocated buffer info */
        manifest_vm->boot_address     = ipa_addr(ipa_begin);
        manifest_vm->kernel_addr_pa   = begin;
        manifest_vm->kernel_size      = memsize;
        manifest_vm->kernel_file_size = filesize;
    }
    /* kernel buffer allocation & VM mapping already handled by caller */
    else {
        RET(pa_difference(begin, end) < memsize, false,
            "kernel larger than available memory\n");

        ans = copy_to_unmapped(stage1_locked, begin, &kernel, ppool);
        RET(!ans, false, "unable to copy kernel from CPIO\n");
    }

    return true;
}

/* load_ramdisk - Loads the VM's ramdisk
 *  @stage1_locked   : Currently locked stage-1 page table of the HV
 *  @begin           : ramdisk buffer start address (NULL if not yet allocated)
 *  @end             : ramdisk buffer end address (NULL if not yet allocated)
 *  @ipa_begin       : Guest physical address where ramdisk is mapped
 *  @manifest_vm     : Ptr to the VM manifest structure
 *  @cpio            : Ptr to the CPIO archive containing the ramdisk
 *  @ppool           : Memory pool
 *  @fdt_size        : Location where ramdisk size is stored (can be NULL)
 *
 *  @return : true if everything went well; false otherwise
 */
static bool
load_ramdisk(struct mm_stage1_locked stage1_locked,
             paddr_t                 begin,
             paddr_t                 end,
             ipaddr_t                ipa_begin,
             struct manifest_vm      *manifest_vm,
             const struct memiter    *cpio,
             struct mpool            *ppool,
             size_t                  *ramdisk_size)
{
    struct memiter ramdisk;  /* ramdisk buffer */
    size_t         size;     /* ramdisk size   */
    void           *ans_p;   /* answer         */
    bool           ans;      /* answer         */

    /* determine size & location of ramdisk file in CPIO archive */
    ans = cpio_get_file(cpio, &manifest_vm->ramdisk_filename, &ramdisk);
    RET(!ans, false, "unable to find ramdisk file \"%s\"\n",
        string_data(&manifest_vm->ramdisk_filename));

    size = memiter_size(&ramdisk);
    if (ramdisk_size)
        *ramdisk_size = size;

    /* ramdisk buffer allocation & VM mapping handled by us */
    if (begin.pa == pa_init(0).pa || end.pa == pa_init(0).pa) {
        dlog_debug("allocating memory for ramdisk (size=%#x)\n", size);

        /* force buffer allocation to match ramdisk's IPA */
        if (manifest_vm->identity_mapping) {
            begin = pa_from_ipa(ipa_begin);
            end   = pa_from_ipa(ipa_add(ipa_begin, size));

            ans_p = mm_identity_map_and_reserve(stage1_locked, begin, end,
                        MM_MODE_R | MM_MODE_W | MM_MODE_X, ppool);
            RET(!ans_p, false, "unable to create direct mapping: [%#x - %#x]\n",
                begin.pa, end.pa);
        } else {
            begin = pa_init(pma_aligned_alloc(
                                stage1_locked.ptable, ipa_begin,
                                size, PAGE_LEVEL_BITS,
                                MM_MODE_R | MM_MODE_W | MM_MODE_X,
                                HYPERVISOR_ID, ppool));
        }

        RET(begin.pa == pma_get_fault_ptr(), false,
            "unable to allocate memory for VM's ramdisk\n");

        ans = copy_to_allocated(begin, &ramdisk);
        RET(!ans, false, "unable to copy ramdisk from CPIO\n");

        /* reassign HV allocated buffer (containing ramdisk) to the VM */
        pma_assign(&manifest_vm->vm->ptable, begin.pa, ipa_begin,
                   pma_get_size(begin.pa, HYPERVISOR_ID),
                   MM_MODE_R | MM_MODE_W | MM_MODE_X,
                   manifest_vm->vm->id, ppool);
        pma_free(stage1_locked.ptable, begin.pa, HYPERVISOR_ID, ppool);

        /* update manifest with newly allocated buffer info */
        manifest_vm->ramdisk_addr_pa = begin;
        manifest_vm->ramdisk_size    = size;
    }
    /* ramdisk buffer allocation & VM mapping already handled by caller */
    else {
        RET(pa_difference(begin, end) < size, false,
            "ramdisk larger than available memory\n");

        ans = copy_to_unmapped(stage1_locked, begin, &ramdisk, ppool);
        RET(!ans, false, "unable to copy ramdisk from CPIO\n");
    }

    return true;
}

/* load_common - Common initialization path for primary / secondary VMs
 *  @stage1_locked   : Currently locked stage-1 page table of the HV
 *  @vm_locked       : Currently locked VM
 *  @manifest_vm     : Ptr to the VM manifest structure
 *  @ppool           : Memory pool
 *
 *  @return : true if everything went well; false otherwise
 */
static bool
load_common(struct mm_stage1_locked stage1_locked,
            struct vm_locked        vm_locked,
            struct manifest_vm      *manifest_vm,
            struct mpool            *ppool)
{
    struct device_region        *dev_region; /* memory mapped device region   */
    struct interrupt            interrupt;   /* interrupt associated to dev   */
    struct interrupt_descriptor int_desc;    /* interrupt descriptor          */
    uint64_t                    vm_int = 0;  /* VM physical interrupts        */
    bool                        ans;         /* answer                        */

    vm_locked.vm->smc_whitelist = manifest_vm->smc_whitelist;
    vm_locked.vm->uuid          = manifest_vm->uuid;

    /* handle interrupt allocations for each device */
    for (size_t i = 0; i < manifest_vm->dev_region_count; i++) {
        dev_region = &manifest_vm->dev_regions[i];

        dlog_info("VM: %#x, device region: %d, name: %s\n",
                  manifest_vm->vm->id, i, dev_region->name);

        /* check for exceeded interrupt allowance (per dev / VM) */
        DIE(dev_region->interrupt_count > SP_MAX_INTERRUPTS_PER_DEVICE,
            "device %d exceeded assigned interrupt quota\n", i);
        DIE(vm_int + dev_region->interrupt_count > VM_MANIFEST_MAX_INTERRUPTS,
            "VM %#x exceeded assigned interrupt quota\n",
            manifest_vm->vm->id);

        /* register each interrupt assigned to current device */
        for (size_t j = 0; j < dev_region->interrupt_count; j++) {
            interrupt = dev_region->interrupts[j];
            int_desc  = infer_interrupt(interrupt);

            vm_locked.vm->interrupt_desc[vm_int++] = int_desc;

            /* configure the physical interrupt */
            plat_interrupts_configure_interrupt(int_desc);
        }
    }

    dlog_verbose("VM %#x has %d physical interrupts defined in manifest.\n",
                 manifest_vm->vm->id, vm_int);

    /* initialize architecture-specific features. */
    arch_vm_features_set(vm_locked.vm);
    ans = plat_iommu_attach_peripheral(stage1_locked, vm_locked,
                                       manifest_vm, ppool);
    RET(!ans, false, "unable to attach upstream peripheral device\n");

    return true;
}

/* load_vm - Helper function that initializes a single VM
 *  @stage1_locked : Currently locked stage-1 page table of the HV
 *  @manifest_vm   : Manifest data pertaining to the VM in question
 *  @vm            : Target VM
 *  @cpio          : Ptr to the CPIO archive
 *  @params        : Kernel boot settings
 *  @ppool         : Memory pool
 *
 *  @return : true if everything went well; false otherwise
 */
static bool
load_vm(struct mm_stage1_locked stage1_locked,
        struct manifest_vm      *manifest_vm,
        struct vm               *vm,
        const struct memiter    *cpio,
        struct boot_params      *params,
        struct mpool            *ppool)
{
    uintpaddr_t        *component_begin[3] = { NULL, NULL, NULL };
    size_t             *component_size[3]  = { NULL, NULL, NULL };
    paddr_t            kernel_start;     /* kernel start (physical) addr     */
    paddr_t            kernel_end;       /* kernel end   (physical) addr     */
    ipaddr_t           kernel_entry;     /* kernel IPA entry point           */
    uintpaddr_t        ipa_vm_mem_begin; /* VM's IPA region start            */
    uintpaddr_t        ipa_vm_mem_end;   /* VM's IPA region end              */
    uintpaddr_t        freeram_begin;    /* start of empty RAM region IPA    */
    size_t             freeram_size;     /* size of empty RAM region         */
    uintptr_t          freeram_ptr;      /* result of RAM region mapping     */
    struct vm_locked   vm_locked;        /* currently locked VM structure    */
    struct vcpu_locked vcpu_locked;      /* currently locked vCPU structure  */
    paddr_t            begin;            /* start address of a memory region */
    paddr_t            end;              /* end address of a memory region   */
    paddr_t            vgic_end;         /* end address of virtual GIC       */
    bool               ret;              /* function's return value          */
    bool               ans;              /* answer                           */
    void               *ans_p;           /* answer                           */

    /* determine VM's kernel address ranges & entry point */
    kernel_start = (manifest_vm->boot_address == MANIFEST_INVALID_ADDRESS)
                 ? layout_primary_begin()
                 : pa_init(manifest_vm->boot_address);
    kernel_entry = ipa_from_pa(kernel_start);

    kernel_end = pa_add(kernel_start, (size_t) RSIZE_MAX);

    dlog_debug("VM: %#x, kernel (\"%s\") address: %#x - %#x\n",
               vm->id, string_data(&manifest_vm->kernel_filename),
               kernel_start, kernel_end);

    /* TODO: confirm that we are definitively replacing core selection *
     *       with a build time solution; also, implement that solution *
     * NOTE: whatever decision we make for deciding the VM IPA range,  *
     *       it needs to be consistent with the CPU paching one        */

    /* lock resource while configuring VM */
    vm_locked = vm_lock(vm);
    manifest_vm->vm = vm;

    /* from now on, assume something will go wrong */
    ret = false;

    /* the amount of RAM that is made known to the VM is specified in the
     * manifest. however, the starting Guest Physical Address (i.e. IPA)
     * is decided here, based on which component is supposed to be mapped
     * at the lowest address. note that these must coincide with the reg
     * values of the `memory` node in the FDT (which is hardcoded).
     *
     * TODO: fix this mess
     */
    if (manifest_vm->mem_layout.kernel < manifest_vm->mem_layout.fdt
    &&  manifest_vm->mem_layout.kernel < manifest_vm->mem_layout.ramdisk)
    {
        ipa_vm_mem_begin  = manifest_vm->mem_layout.kernel;
        component_size[0] = &manifest_vm->kernel_size;
    } else if (manifest_vm->mem_layout.fdt < manifest_vm->mem_layout.kernel
           &&  manifest_vm->mem_layout.fdt < manifest_vm->mem_layout.ramdisk)
    {
        ipa_vm_mem_begin  = manifest_vm->mem_layout.fdt;
        component_size[0] = &manifest_vm->fdt_size;
    } else {
        ipa_vm_mem_begin  = manifest_vm->mem_layout.ramdisk;
        component_size[0] = &manifest_vm->ramdisk_size;
    }
    component_begin[0] = &ipa_vm_mem_begin;
    ipa_vm_mem_end     = ipa_vm_mem_begin + manifest_vm->memory_size;


    /* kernel sanity checks */
    GOTO(string_is_empty(&manifest_vm->kernel_filename), out,
         "VM: %#x, no kernel specified\n", vm->id);
    GOTO(manifest_vm->mem_layout.kernel + manifest_vm->kernel_size
         > ipa_vm_mem_end,
         out, "VM: %#x, kernel falls outside IPA range\n", vm->id);

    /* load the kernel */
    ans = load_kernel(stage1_locked, pa_init(0), pa_init(0),
                      ipa_init(manifest_vm->mem_layout.kernel),
                      manifest_vm, cpio, ppool, NULL);
    GOTO(!ans, out, "VM: %#x, unable to load kernel \"%s\"\n",
         vm->id, string_data(&manifest_vm->kernel_filename));

    /* update ordered list of components */
    if (*component_begin[0] != manifest_vm->mem_layout.kernel) {
        component_begin[1] = &manifest_vm->mem_layout.kernel;
        component_size[1]  = &manifest_vm->kernel_size;
    }

    dlog_debug("VM: %#x, kernel has been loaded\n");


    /* FDT sanity checks */
    GOTO(string_is_empty(&manifest_vm->fdt_filename), fdt_load_done,
         "VM: %#x, skipping unspecified FDT\n", vm->id);
    GOTO(manifest_vm->mem_layout.fdt
         + pma_get_size(pa_addr(manifest_vm->fdt_addr_pa), manifest_vm->vm->id)
         > ipa_vm_mem_end,
         out, "VM: %#x, FDT falls outside IPA range\n", vm->id);
    GOTO(manifest_vm->mem_layout.fdt == MANIFEST_INVALID_ADDRESS,
         out, "VM: %#x, FDT IPA not specified in manifest\n", vm->id);

    /* load the FDT */
    ans =  load_fdt(stage1_locked, pa_init(0), pa_init(0),
                    ipa_init(manifest_vm->mem_layout.fdt),
                    manifest_vm, cpio, ppool, NULL, &(params->kernel_arg));
    GOTO(!ans, out, "VM: %#x, unable to load FDT \"%s\"\n",
         vm->id, string_data(&manifest_vm->fdt_filename));

    /* update ordered list of compoenents */
    if (*component_begin[0] != manifest_vm->mem_layout.fdt) {
        /* fdt is not first component */
        if (component_begin[1] == NULL) {
            /* this case is entered if kernel is first */
            component_begin[1] = &manifest_vm->mem_layout.fdt;
            component_size[1]  = &manifest_vm->fdt_size;

        } else if (manifest_vm->mem_layout.fdt < *component_begin[1]) {
            /* ramdisk is first and kernel has higher address than fdt */
            component_begin[2] = component_begin[1];
            component_size[2]  = component_size[1];
            component_begin[1] = &manifest_vm->mem_layout.fdt;
            component_size[1]  = &manifest_vm->fdt_size;
        } else {
            /* ramdisk is first and kernel has lower address than fdt */
            component_begin[2] = &manifest_vm->mem_layout.fdt;
            component_size[2]  = &manifest_vm->fdt_size;
        }
    }

    dlog_debug("VM: %#x, FDT has been loaded\n");

fdt_load_done:

    /* ramdisk sanity checks */
    GOTO(string_is_empty(&manifest_vm->ramdisk_filename), ramdisk_load_done,
         "VM: %#x, skipping unspecified ramdisk\n", vm->id);
    GOTO(manifest_vm->mem_layout.ramdisk
         + pma_get_size(pa_addr(manifest_vm->ramdisk_addr_pa),
                        manifest_vm->vm->id)
         > ipa_vm_mem_end,
         out, "VM: %#x, ramdisk falls outisde IPA range\n", vm->id);
    GOTO(manifest_vm->mem_layout.ramdisk == MANIFEST_INVALID_ADDRESS,
         out, "VM: %#x, ramdisk IPA not specified in manifest\n", vm->id);

    /* load the ramdisk */
    ans =  load_ramdisk(stage1_locked, pa_init(0), pa_init(0),
            ipa_init(manifest_vm->mem_layout.ramdisk),
            manifest_vm, cpio, ppool, NULL);
    GOTO(!ans, out, "VM: %#x, unable to load ramdisk \"%s\"\n",
        vm->id, string_data(&manifest_vm->ramdisk_filename));

    /* save ramdisk region limits to inform kernel at boot */
        params->initrd_begin.pa = pa_addr(manifest_vm->ramdisk_addr_pa);
        params->initrd_end.pa   = pa_addr(manifest_vm->ramdisk_addr_pa) +
                                  pma_get_size(
                                    pa_addr(manifest_vm->ramdisk_addr_pa),
                                    manifest_vm->vm->id);

    /* update ordered list of compoenents */
    if (*component_begin[0] != manifest_vm->mem_layout.ramdisk) {
        /* ramdisk is not first component, so add it at correct position */
        if (component_begin[1] == NULL) {
            /* this case is entered if kernel is first and VM has no fdt */
            component_begin[1] = &manifest_vm->mem_layout.ramdisk;
            component_size[1]  = &manifest_vm->ramdisk_size;

        } else if (manifest_vm->mem_layout.ramdisk < *component_begin[1]) {
             /* ramdisk has lower address than second component */
            component_begin[2] = component_begin[1];
            component_size[2]  = component_size[1];
            component_begin[1] = &manifest_vm->mem_layout.ramdisk;
            component_size[1]  = &manifest_vm->ramdisk_size;
        } else {
             /* ramdisk has highest address */
            component_begin[2] = &manifest_vm->mem_layout.ramdisk;
            component_size[2]  = &manifest_vm->ramdisk_size;
        }
    }

    dlog_debug("VM: %#x, ramdisk has been loaded\n");

ramdisk_load_done:

    /* check memory layout for potential component overlap */
    for (size_t i = 0; i < 2 && component_begin[i + 1]; i++) {
        GOTO(*component_begin[i] + *component_size[i] > *component_begin[i + 1],
             out, "VM: %#x, invalid memory layout\n", vm->id);
    }

    /* map the gaps between components as free RAM */
    dlog_debug("VM: %#x, allocating free memory space\n", vm->id);
    for (size_t i = 0; i < 3 && component_begin[i]; i++) {
        freeram_begin = mm_round_up_to_page(*component_begin[i] +
                                            *component_size[i]);
        freeram_size  = mm_round_down_to_page(
                            (i == 2 || component_begin[i + 1] == NULL)
                            ? ipa_vm_mem_end - freeram_begin
                            : *component_begin[i + 1] - freeram_begin);
        if (!freeram_size)
            continue;

        if (manifest_vm->identity_mapping) {
            begin = pa_init(freeram_begin);
            end   = pa_add(begin, freeram_size);

            freeram_ptr = vm_identity_map_and_reserve(vm_locked, begin, end,
                                MM_MODE_R | MM_MODE_W | MM_MODE_X, ppool, NULL);
            GOTO(!freeram_ptr, out, "VM: %#x, unable to create direct mapping "
                 "[%#x - %#x]\n", vm->id, begin.pa, end.pa);
        } else {
            freeram_ptr = pma_aligned_alloc_with_split(&vm_locked.vm->ptable,
                            ipa_init(freeram_begin), freeram_size,
                            PMA_ALIGN_AUTO_PAGE_LVL,
                            MM_MODE_R | MM_MODE_W | MM_MODE_X,
                            vm->id, ppool, 16);
            GOTO(freeram_ptr == pma_get_fault_ptr(), out,
                 "VM: %#x, unable to allocate freeram memory\n", vm->id);
        }
    }

    /* map GIC to hypervisor, not to primary VM                     *
     * NOTE: check project/sanctuary/BUILD.gn for GIC_* definitions *
     *       also, look at build/toolchain/embedded.gni             *
     *                                                              *
     * TODO: is it ok to double map the GIC to the HV page table?   *
     *       this happens if any secondary VMs are present.         */
    ans_p = mm_identity_map(stage1_locked,
                            pa_init(GIC_START), pa_init(GIC_END),
                            MM_MODE_R | MM_MODE_W | MM_MODE_D, ppool);
    GOTO(!ans_p, out, "unable to map GIC to hypervisor address space\n");
    dlog_debug("GIC mapped to hypervisor address space: [%#x - %#x]\n",
               GIC_START, GIC_END);


    /* vGIC sanity checks                                    *
     * NOTE: assume that vGIC is unmapped until proven wrong */
    manifest_vm->vm->vgic = NULL;

    GOTO(manifest_vm->mem_layout.gic == MANIFEST_INVALID_ADDRESS, gic_load_done,
         "VM: %#x, vGIC mapping not specified in manifest\n", vm->id);
    GOTO(manifest_vm->mem_layout.gic + (sizeof(struct virt_gic)) - 1
         >= manifest_vm->mem_layout.kernel
      && manifest_vm->mem_layout.kernel + manifest_vm->memory_size
         > manifest_vm->mem_layout.gic,
         out, "VM: %#x, vGIC must reside outisde VM's RAM IPA range\n", vm->id);

    /* allocate memory for vGIC */
    manifest_vm->vm->vgic = (void *) pma_aligned_alloc(stage1_locked.ptable,
                                        ipa_init(manifest_vm->mem_layout.gic),
                                        sizeof(struct virt_gic),
                                        PMA_ALIGN_AUTO_PAGE_LVL,
                                        MM_MODE_R | MM_MODE_W | MM_MODE_D,
                                        HYPERVISOR_ID, ppool);
    GOTO(((uintptr_t) manifest_vm->vm->vgic) == pma_get_fault_ptr(),
         out, "VM: %#x, unable to allocate vGIC physical memory\n", vm->id);

    /* map vGIC to VM's IPA space */
    vgic_end = pa_init((uintpaddr_t)(manifest_vm->vm->vgic) +
                               (uintpaddr_t)(sizeof(struct virt_gic)) - 1);

    ans = mm_vm_prepare(&manifest_vm->vm->ptable,
                        ipa_init(manifest_vm->mem_layout.gic),
                        pa_init((uintpaddr_t) manifest_vm->vm->vgic),
                        vgic_end, MM_MODE_D, ppool);
    GOTO(!ans, out, "VM: %#x, unable to map vGIC to VM's page table\n", vm->id);

    mm_vm_commit(&manifest_vm->vm->ptable,
                 ipa_init(manifest_vm->mem_layout.gic),
                 pa_init((uintpaddr_t) manifest_vm->vm->vgic),
                 vgic_end, MM_MODE_D, ppool, NULL);
    init_vgic(manifest_vm->vm);

    dlog_debug("VM: %#x, vGIC mapped to VM's IPA space\n", vm->id);

gic_load_done:


#if RELEASE == 0
    pma_print_chunks();
#endif

    /* save the allocated memory chunks                   *
     * (i.e.: kernel memory, ramdisk, etc.) as mem_ranges */
    for (size_t i = 0; i < params->mem_ranges_count; ++i) {
        params->mem_ranges[i].begin = pa_init(0);
        params->mem_ranges[i].end   = pa_init(0);
    }

    /* store allocated IPA memory range in VM struct */
    manifest_vm->vm->ipa_mem_begin = ipa_init(manifest_vm->mem_layout.kernel);
    manifest_vm->vm->ipa_mem_end   = ipa_add(manifest_vm->vm->ipa_mem_begin,
                                             manifest_vm->memory_size);

    /* map device memory as non-executable */
    for (size_t i = 0; i < params->device_mem_ranges_count; ++i) {
        ans = vm_identity_map(vm_locked, params->device_mem_ranges[i].begin,
                              params->device_mem_ranges[i].end,
                              MM_MODE_R | MM_MODE_W | MM_MODE_D,
                              ppool, NULL);
        GOTO(!ans, out, "VM: %#x, unable to initialize dev memory\n", vm->id);
    }

    /* initialize backing physical devices for virtual device instances *
     * most require mapping them to the hypervisor, not individual VMs  *
     *                                                                  *
     * NOTE: vGIC gets special treatment for now                        */
    ans = init_backing_devs(stage1_locked, ppool);
    GOTO(ans, out, "VM: %#x, unable to initialize backing physical "
         "devs for emulation\n", vm->id);

    /* initialize virtual devices */
    ans = init_virt_devs();
    GOTO(ans, out, "VS: %#x, unable to initialize virtual devices\n", vm->id);

    dlog_debug("VM: %#x, loaded with %u vCPUs, entry at PA=%#x IPA=%#x.\n",
               vm->id, vm->vcpu_count, pa_addr(kernel_start),
               manifest_vm->boot_address);

    /* add intialized VM to boot list  *
     * assume boot order set by caller */
    vm_update_boot(vm);

    /* initialize VM's primary vCPU state */
    vcpu_locked = vcpu_lock(vm_get_vcpu(vm, 0));

    vcpu_on(vcpu_locked, ipa_from_pa(pa_init(manifest_vm->boot_address)),
            params->kernel_arg);
    vcpu_unlock(&vcpu_locked);

    /* success */
    ret = true;

out:
    vm_unlock(&vm_locked);

    return ret;
}

/* load_vms - Initializes all VMs but does not start them
 *  @stage1_locked : Currently locked stage-1 page table of the HV
 *  @manifest      : Manifest data pertaining to all VMs (and more)
 *  @cpio          : Ptr to the CPIO archive
 *  @boot_params   : Ptr to kernel boot parameters (not boot args)
 *  @update        : Ptr to kernel boot parameters that need updating
 *  @ppool         : Memory pool
 *
 *  @return : true if everything went well; false otherwise
 */
bool
load_vms(struct mm_stage1_locked   stage1_locked,
         struct manifest           *manifest,
         const struct memiter      *cpio,
         struct boot_params        *params,
         struct boot_params_update *update __attribute__((unused)),
         struct mpool              *ppool)
{
    struct manifest_vm *manifest_vm;  /* VM-specific manifest data     */
    struct vm          *primary_vm;   /* ptr to primary VM (aka. dom0) */
    struct vm          *vm;           /* iterator over secondary VMs   */
    uint16_t           vm_id;         /* secondary VM id               */
    bool               ans;           /* answer                        */

    /* sanity check: we need at least the primary VM */
    RET(!manifest->vm_count, false,
        "expected at least primary VM in manifest\n");

    /* load primary VM */
    ans = vm_init_next(manifest->vm[PG_PRIMARY_VM_INDEX].vcpu_count,
                       manifest->vm[PG_PRIMARY_VM_INDEX].cpu_count,
                       manifest->vm[PG_PRIMARY_VM_INDEX].cpus,
                       ppool, &primary_vm);
    RET(!ans, false, "unable to initialize primary VM\n");

    ans = load_vm(stage1_locked, &manifest->vm[PG_PRIMARY_VM_INDEX],
                  primary_vm, cpio, params, ppool);
    RET(!ans, false, "unable to load primary VM\n");

    /* load remaining secondary VMs */
    for (size_t i = 0; i < manifest->vm_count - 1; ++i) {
        vm_id       = PG_VM_ID_OFFSET + i;
        manifest_vm = &manifest->vm[vm_id];

        dlog_info("Loading VM id %#x: %s.\n",
                  vm_id, manifest_vm->debug_name);

        ans = vm_init_next(manifest_vm->vcpu_count,
                           manifest_vm->cpu_count,
                           manifest_vm->cpus,
                           ppool, &vm);
        RET(!ans, false, "unable to initialize secondary VM %#x\n", vm_id);

        ans = load_vm(stage1_locked, &manifest->vm[vm_id],
                      vm, cpio, params, ppool);
        RET(!ans, false, "unable to load secondary VM %#x\n", vm_id);
    }

    return true;
}

/* load_devices - Maps physical devices & related interrupts to specific VM
 *  @stage1_locked : Currently locked stage-1 page table of the HV
 *  @manifest_vm   : Manifest data pertaining to the VM in question
 *  @ppool         : Memory pool
 *
 *  @return : true if everything went well; false otherwise
 */
bool
load_devices(struct mm_stage1_locked stage1_locked,
             struct manifest_vm      *manifest_vm,
             struct mpool            *ppool)
{
    struct vm_locked     vm_locked;   /* VM structure with acquired lock */
    struct device_region *dev_region; /* identity mapped device region   */
    bool                 ret;         /* function's return value         */
    bool                 ans;         /* answer                          */

    /* acquire lock for this VM; assume worst is to happen from now on */
    vm_locked = vm_lock(manifest_vm->vm);
    ret       = false;

    dlog_debug("VM: %#x, assigning device memory\n", manifest_vm->vm->id);

    /* map device memory to VM address space */
    for (size_t i = 0; i < manifest_vm->dev_region_count; ++i) {
        dev_region = &manifest_vm->dev_regions[i];

        ans = vm_identity_map(vm_locked, pa_init(dev_region->base_address),
                              pa_init(dev_region->base_address +
                                      (PAGE_SIZE * dev_region->page_count)),
                              dev_region->attributes, ppool, NULL);
        GOTO(!ans, out, "VM: %#x, unable to initialize device memory\n",
             manifest_vm->vm->id);
    }

    /* assign interrupts etc. */
    ans = load_common(stage1_locked, vm_locked, manifest_vm, ppool);
    GOTO(!ans, out, "VM: %#x, unable to configure interrupts\n",
         manifest_vm->vm->id);

    /* success */
    ret = true;

out:
    vm_unlock(&vm_locked);
    return ret;
}

