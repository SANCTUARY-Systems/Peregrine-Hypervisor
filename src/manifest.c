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

#ifndef EXTERNAL_INCLUDE
#   include "pg/arch/init.h"
#   include "pg/manifest.h"
#   include "pg/addr.h"
#   include "pg/check.h"
#   include "pg/dlog.h"
#   include "pg/fdt.h"
#   include "pg/static_assert.h"
#   include "pg/std.h"
#   include "pg/pma.h"
#   include "pg/manifest_util.h"
#   include "pg/uuid.h"
#   include "pg/error.h"
#endif

/******************************************************************************
 ************************* INTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* count_digits - Returns the number of digit in the decimal representation
 *  @vm_id : Target number
 *
 *  @return : Number of digits
 */
static inline size_t
count_digits(uint16_t vm_id)
{
    size_t digits = 0;

    do {
        digits++;
        vm_id /= 10;
    } while (vm_id);

    return digits;
}

/* generate_vm_node_name - Generates VM name string
 *  @str   : Buffer of at least VM_NAME_BUF_SIZE
 *  @vm_id : VM ID
 */
static void
generate_vm_node_name(const struct string *str, uint16_t vm_id)
{
    static const char *digits      = "0123456789";
    size_t            vm_id_digits = count_digits(vm_id);
    const char        *base        = str->data;
    char              *ptr         = (char *) base + (VM_NAME_EXTRA_CHARS +
                                                      vm_id_digits);

    DIE(vm_id_digits > VM_ID_MAX_DIGITS, "resulting VM name is too long");

    *(--ptr) = '\0';
    do {
        *(--ptr) = digits[vm_id % 10];
        vm_id /= 10;
    } while (vm_id);
    *(--ptr) = 'm';
    *(--ptr) = 'v';

    DIE(ptr != base, "");
}

/* parse_device_region_node - Parses VM's device list from manifest
 *  @vm_id       : ID of target VM
 *  @dev_node    : Parent FDT node of all device nodes
 *  @dev_regions : Reference to device list buffer (passed for initialization)
 *  @count       : Reference to number of devices
 *
 *  @return : MANIFEST_SUCCESS if everything went well
 */
static enum manifest_return_code
parse_device_region_node(uint16_t             vm_id,
                         struct fdt_node      *dev_node,
                         struct device_region *dev_regions,
                         uint8_t              *count)
{
    enum manifest_return_code ans;      /* answer                      */
    struct uint32list_iter    list;     /* device interrupt list       */
    uint8_t                   i = 0;    /* device list iteration index */
    uint8_t                   j = 0;    /* misc. iteration index       */

    dlog_debug("  Partition Device Regions\n");

    /* sanity check */
    RET(!fdt_first_child(dev_node), MANIFEST_ERROR_DEVICE_REGION_NODE_EMPTY,
        "no child node found");

    /* iterate over all devices and fill manifest */
    do {
        dlog_debug("    Device Region[%u], %p\n", i, dev_regions);

        /* extract relevant properties */
        ans = read_optional_string(vm_id, dev_node, "description",
                                   &(dev_regions[i].name));

        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"description\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Name: %s\n", dev_regions[i].name.data);

        ans = read_uint64(vm_id, dev_node, "base-address",
                          &dev_regions[i].base_address);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"base-address\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Base address:  %#x\n", dev_regions[i].base_address);

        ans = read_uint32(vm_id, dev_node, "pages-count",
                          &dev_regions[i].page_count);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"pages-count\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Pages_count:  %u\n", dev_regions[i].page_count);

        ans = read_uint32(vm_id, dev_node, "attributes",
                          &dev_regions[i].attributes);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"attributes\" property (%s)\n",
            manifest_strerror(ans));
        dev_regions[i].attributes =
            (dev_regions[i].attributes & MM_PERM_MASK) | MM_MODE_D;
        dlog_debug("    Attributes:  %u\n", dev_regions[i].attributes);

        ans = read_optional_uint32list(vm_id, dev_node, "interrupts", &list);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"interrupts\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Interrupt List:\n");

        /* iterate over interrupt list; each element composed of 2 integers: *
         *    interrupt_id, attributes                                       */
        j = 0;
        while (uint32list_has_next(&list) && j < SP_MAX_INTERRUPTS_PER_DEVICE) {
            ans = uint32list_get_next(&list,
                                      &dev_regions[i].interrupts[j].id);
            RET(ans != MANIFEST_SUCCESS, ans,
                "unable to get Interrupt ID from list (%s)\n",
                manifest_strerror(ans));

            RET(!uint32list_has_next(&list),
                MANIFEST_ERROR_MALFORMED_INTEGER_LIST,
                "malformed interrupt list; missing attributes\n");

            ans = uint32list_get_next(&list,
                        &dev_regions[i].interrupts[j].attributes);
            RET(ans != MANIFEST_SUCCESS, ans,
                "unable to get Interrupt Attr from list (%s)\n",
                manifest_strerror(ans));

            dlog_debug("      ID = %u, attributes = %u\n",
                       dev_regions[i].interrupts[j].id,
                       dev_regions[i].interrupts[j].attributes);
            j++;
        }

       dev_regions[i].interrupt_count = j;

        /* resume extraction of relevant properties */
        ans = read_optional_uint32(vm_id, dev_node, "smmu-id",
                    MANIFEST_INVALID_ID, &dev_regions[i].smmu_id);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"smmu-id\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    smmu-id:  %u\n", dev_regions[i].smmu_id);

        ans = read_optional_uint32list(vm_id, dev_node, "stream-ids", &list);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"stream-ids\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Stream IDs assigned:\n");

        /* iterate over streams list */
        j = 0;
        while (uint32list_has_next(&list) && j < SP_MAX_STREAMS_PER_DEVICE) {
            ans = uint32list_get_next(&list, &dev_regions[i].stream_ids[j]);
            RET(ans != MANIFEST_SUCCESS, ans,
                "unable to get Stream ID from list (%s)\n",
                manifest_strerror(ans));

            dlog_debug("      %u\n", dev_regions[i].stream_ids[j]);
            j++;
        }

        dev_regions[i].stream_count = j;

        /* finish up extraction of relevant properties */
        ans = read_bool(vm_id, dev_node, "exclusive-access",
                        &dev_regions[i].exclusive_access);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access \"exclusive-access\" property (%s)\n",
            manifest_strerror(ans));
        dlog_debug("    Exclusive_access: %u\n",
                   dev_regions[i].exclusive_access);

        i++;
    } while (fdt_next_sibling(dev_node));

    /* return number of devices */
    *count = i;

    return MANIFEST_SUCCESS;
}

/* parse_vm - Parses a subsection of the FDT, pertaining to a certain VM
 *  @node  : Manifest FDT node containing VM data
 *  @vm    : VM manifest sub-structure to be initialized
 *  @vm_id : ID of target VM
 *
 *  @return : MANIFEST_SUCCESS if everything went well
 */
static enum manifest_return_code
parse_vm(struct fdt_node    *node,
         struct manifest_vm *vm,
         uint16_t           vm_id)
{
    enum manifest_return_code ans;
    bool                      ans_b;

    const struct string    mem_layout_node_name = { "ipa-memory-layout" };
    const struct string    dev_region_node_name = { "device-regions" };
    struct uint32list_iter smcs;
    struct uint32list_iter cpus;
    size_t                 idx_smc;

    size_t                  idx_secserv = 0;
    struct chararrlist_iter secservs;
    char                    *secserv_str;
    size_t                  secserv_size;

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    char                    wl_str[UUID_STR_SIZE + 1];
#endif 

    /* begin extracting relevant data */
    ans = read_optional_uuid(vm_id, node, "uuid", &vm->uuid);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"uuid\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_string(vm_id, node, "debug_name", &vm->debug_name);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"debug_name\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_string(vm_id, node, "kernel_filename",
                               &vm->kernel_filename);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"kernel_filename\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(vm_id, node, "kernel_version", 0,
                               &vm->kernel_version);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"kernel_version\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_char_arr_sh(vm_id, node, "kernel_boot_params",
                                    &vm->kernel_boot_params);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"kernel_boot_params\" property (%s)\n",
        manifest_strerror(ans));

#if MEASURED_BOOT
    ans = read_optional_char_arr(vm_id, node, "kernel_hash", &vm->kernel_hash);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"kernel_hash\" property (%s)\n",
        manifest_strerror(ans));
#endif

    ans = read_optional_string(vm_id, node, "fdt_filename", &vm->fdt_filename);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"fdt_filename\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(vm_id, node, "fdt_version", 0, &vm->fdt_version);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"fdt_version\" property (%s)\n",
        manifest_strerror(ans));

#if MEASURED_BOOT
    ans = read_optional_char_arr(vm_id, node, "fdt_hash", &vm->fdt_hash);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"fdt_hash\" property (%s)\n",
        manifest_strerror(ans));
#endif

    ans = read_optional_string(vm_id, node, "ramdisk_filename",
                               &vm->ramdisk_filename);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"ramdisk_filename\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(vm_id, node, "ramdisk_version", 0,
                               &vm->ramdisk_version);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"ramdisk_version\" property (%s)\n",
        manifest_strerror(ans));

#if MEASURED_BOOT
    ans = read_optional_char_arr(vm_id, node, "ramdisk_hash",
                                 &vm->ramdisk_hash);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"ramdisk_hash\" property (%s)\n",
        manifest_strerror(ans));
#endif

    ans = read_bool(vm_id, node, "is_primary", &vm->is_primary);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"is_primary\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_uint16(vm_id, node, "vcpu_count", &vm->vcpu_count);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"vcpu_count\" property (%s)\n",
        manifest_strerror(ans));

    /* parse physical CPU list                                   *
     * NOTE: having at least one CPU per VM should be guaranteed */
    vm->cpu_count = 0;
    ans = read_uint32list(vm_id, node, "cpus", &cpus);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"cpus\" property (%s)\n",
        manifest_strerror(ans));

    while (uint32list_has_next(&cpus)) {
        if (vm->cpu_count == MAX_CPUS) {
            dlog_warning("Physical CPU list larger than MAX_CPUS (%lu)\n",
                         MAX_CPUS);
            break;
        }

        ans = uint32list_get_next(&cpus, &vm->cpus[vm->cpu_count++]);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access assigned physical CPU list in full (%s)\n",
            manifest_strerror(ans));
    }

    /* parse Secure Monitor Call whitelist */
    ans = read_optional_uint32list(vm_id, node, "smc_whitelist", &smcs);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"smc_whitelist\" property (%s)\n",
        manifest_strerror(ans));

    while (uint32list_has_next(&smcs)) {
        if (vm->smc_whitelist.smc_count == MAX_SMCS) {
            dlog_warning("SMC whitelist larger than MAX_SMCS (%lu)\n",
                         MAX_SMCS);
            break;
        }

        idx_smc = vm->smc_whitelist.smc_count++;
        ans = uint32list_get_next(&smcs, &vm->smc_whitelist.smcs[idx_smc]);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access SMC whitelist in full (%s)\n",
            manifest_strerror(ans));
    }

    ans = read_bool(vm_id, node, "smc_whitelist_permissive",
                    &vm->smc_whitelist.permissive);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"smc_whitelist_permissive\" property (%s)\n",
        manifest_strerror(ans));

    /* parse Security Services whitelist */
    ans = read_optional_chararrlist(vm_id, node, "security_services", &secservs);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"security_services\" property (%s)\n",
        manifest_strerror(ans));

    while (chararrlist_has_next(&secservs)) {
        if (vm->secserv_whitelist.secserv_count == MAX_SECSERVS) {
            dlog_warning("Security Services whitelist larger than MAX_SECSERVS "
                         "(%lu)\n", MAX_SECSERVS);
            break;
        }

        idx_secserv = vm->secserv_whitelist.secserv_count++;
        ans = chararrlist_get_next(&secservs, &secserv_str, &secserv_size);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to access Security Services whitelist in full (%s)\n",
            manifest_strerror(ans));

        ans_b = uuid_from_str(secserv_str, secserv_size,
                              &vm->secserv_whitelist.secservs[idx_secserv]);
        RET(!ans_b, MANIFEST_ERROR_MALFORMED_UUID, "malformed UUID string\n");

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
        ans_b = uuid_to_str(&vm->secserv_whitelist.secservs[idx_secserv],
                            wl_str, sizeof(wl_str));
        RET(!ans_b, MANIFEST_ERROR_MALFORMED_UUID, "malformed UUID string");

        dlog_debug("Security service whitelist entry %lu: %s\n",
                   idx_secserv, wl_str);
#endif
    }

    ans = read_optional_uint64(vm_id, node, "memory_size", 0, &vm->memory_size);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"memory_size\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_bool(vm_id, node, "use_disk_encryption", &vm->use_disk_encryption);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"use_disk_encryption\" property (%s)\n",
        manifest_strerror(ans));

#if MEASURED_BOOT
    ans = read_uint32(vm_id, node, "hash_algo_id", &vm->hash_algo_id);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"hash_algo_id\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_uint16(vm_id, node, "hash_size", &vm->hash_size);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"hash_size\" property (%s)\n",
        manifest_strerror(ans));
#endif

    ans = read_optional_uint64(vm_id, node, "boot_address",
                             MANIFEST_INVALID_ADDRESS,
                             &vm->boot_address);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"boot_address\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_bool(vm_id, node, "requires_identity_mapping",
                    &vm->identity_mapping);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"requires_identity_mapping\" property (%s)\n",
        manifest_strerror(ans));

    /* create copy of node as we need the parent of mem_node *
     * again for device regions                              */
    struct fdt_node mem_node = { node->fdt, node->offset };
    RET(!fdt_find_child(&mem_node, &mem_layout_node_name),
        MANIFEST_ERROR_NO_MEMORY_LAYOUT,
        "unable to find \"ipa_memory_layout\" node\n");

    ans = read_uint64(vm_id, &mem_node, "kernel", &vm->mem_layout.kernel);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"kernel\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint64(vm_id, &mem_node, "gic",
                               MANIFEST_INVALID_ADDRESS, &vm->mem_layout.gic);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"gic\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint64(vm_id, &mem_node, "fdt",
                               MANIFEST_INVALID_ADDRESS, &vm->mem_layout.fdt);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"fdt\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint64(vm_id, &mem_node, "ramdisk",
                               MANIFEST_INVALID_ADDRESS, &vm->mem_layout.ramdisk);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"ramdisk\" property (%s)\n",
        manifest_strerror(ans));

    /* parse VM-specific device regions */
    GOTO(!fdt_find_child(node, &dev_region_node_name), out,
         "no \"device_regions\" node found in VM manifest; skipping...\n");

    ans = parse_device_region_node(vm_id, node, vm->dev_regions,
                                       &vm->dev_region_count);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to parse \"device_regions\" node (%s)\n",
        manifest_strerror(ans));

    dlog_debug("Total %lu device regions found\n",
               (uint64_t) vm->dev_region_count);

out:
    return MANIFEST_SUCCESS;
}

/******************************************************************************
 ************************* EXTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* manifest_strerror - Decodes manifest status value to descriptive message
 *  @ret_code : Manifest operation return code
 *
 *  @ret : Ptr to the appropriate status value description
 */
const char *
manifest_strerror(enum manifest_return_code ret_code)
{
    switch (ret_code) {
        case MANIFEST_SUCCESS:
            return "Success";
        case MANIFEST_ERROR_FILE_SIZE:
            return "Total size in header does not match file size";
        case MANIFEST_ERROR_MALFORMED_DTB:
            return "Malformed device tree blob";
        case MANIFEST_ERROR_NO_ROOT_NODE:
            return "Could not find root node in manifest";
        case MANIFEST_ERROR_NO_HYPERVISOR_FDT_NODE:
            return "Could not find \"hypervisor\" node in manifest";
        case MANIFEST_ERROR_NOT_COMPATIBLE:
            return "Hypervisor manifest entry not compatible with Peregrine";
        case MANIFEST_ERROR_RESERVED_VM_ID:
            return "Manifest defines a VM with a reserved ID";
        case MANIFEST_ERROR_NO_PRIMARY_VM:
            return "Manifest does not contain a primary VM entry";
        case MANIFEST_ERROR_TOO_MANY_VMS:
            return "Manifest specifies more VMs than Peregrine has "
                   "statically allocated space for";
        case MANIFEST_ERROR_PROPERTY_NOT_FOUND:
            return "Property not found";
        case MANIFEST_ERROR_MALFORMED_STRING:
            return "Malformed string property";
        case MANIFEST_ERROR_STRING_TOO_LONG:
            return "String too long";
        case MANIFEST_ERROR_MALFORMED_INTEGER:
            return "Malformed integer property";
        case MANIFEST_ERROR_INTEGER_OVERFLOW:
            return "Integer overflow";
        case MANIFEST_ERROR_MALFORMED_INTEGER_LIST:
            return "Malformed integer list property";
        case MANIFEST_ERROR_MALFORMED_BOOLEAN:
            return "Malformed boolean property";
        case MANIFEST_ERROR_MEMORY_REGION_NODE_EMPTY:
            return "Memory-region node should have at least one entry";
        case MANIFEST_ERROR_DEVICE_REGION_NODE_EMPTY:
            return "Device-region node should have at least one entry";
        case MANIFEST_ERROR_RXTX_SIZE_MISMATCH:
            return "RX and TX buffers should be of same size";
        case MANIFEST_ERROR_MALFORMED_UUID:
            return "Malformed UUID";
        default:
            panic("Unexpected manifest return code.");
            return NULL;
    }
}

/******************************************************************************
 ********************************* PUBLIC API *********************************
 ******************************************************************************/

/* manifest_init - Parses manifest data from FDT
 *  @ppool        : Memory pool
 *  @manifest_ret : Reference to manifest struct to be initialized
 *  @manifest_fdt : Target manifest
 *
 *  @return : MANIFEST_SUCCESS if everything went well
 *
 * TODO: @ppool was previously passed to parse_device_region_node() and
 *       parse_vm() but was ultimately not used. Remove it from the public
 *       API as well after ensuring that we won't need it in the future.
 */
enum manifest_return_code
manifest_init(struct mpool    *ppool __attribute__((unused)),
              struct manifest **manifest_ret,
              struct memiter  *manifest_fdt)
{
    enum manifest_return_code ans;
    struct string             vm_name = {};
    struct fdt                fdt;
    struct fdt_node           hyp_node;
    struct manifest           *manifest = *manifest_ret;
    bool                      found_primary_vm = false;

    /* sanity checks */
    RET(!fdt_init_from_memiter(&fdt, manifest_fdt),
        MANIFEST_ERROR_FILE_SIZE,
        "unable to initialize fdt structure from memory buffer\n");

    RET(!fdt_find_node(&fdt, "/hypervisor", &hyp_node),
        MANIFEST_ERROR_NO_HYPERVISOR_FDT_NODE,
        "unable to find a \"/hypervisor\" node in the FDT\n");

    RET(!fdt_is_compatible(&hyp_node, "peregrine,peregrine"),
        MANIFEST_ERROR_NOT_COMPATIBLE,
        "compatibility check failed\n");

    /* begin extracting relevant data */
    ans = read_optional_uuid(HYPERVISOR_ID, &hyp_node, "manifest_uuid",
                             &manifest->manifest_uuid);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_uuid\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(HYPERVISOR_ID, &hyp_node, "manifest_version",
                               0, &manifest->manifest_version);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_version\" property (%s)\n",
        manifest_strerror(ans));

#if MEASURED_BOOT
    ans = read_optional_uint32(HYPERVISOR_ID, &hyp_node,
                               "manifest_signature_algo_id", 0,
                               &manifest->manifest_signature_algo_id);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_signature_algo_id\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(HYPERVISOR_ID, &hyp_node,
                               "manifest_signature_size", 0,
                               &manifest->manifest_signature_size);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_signature_size\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(HYPERVISOR_ID, &hyp_node, "manifest_hash_size",
                               0, &manifest->manifest_hash_size);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_hash_size\" property (%s)\n",
        manifest_strerror(ans));

    ans = read_optional_uint32(HYPERVISOR_ID, &hyp_node, "manifest_hash_algo_id",
                               0, &manifest->manifest_hash_algo_id);
    RET(ans != MANIFEST_SUCCESS, ans,
        "unable to access \"manifest_hash_algo_id\" property (%s)\n",
        manifest_strerror(ans));
#endif

    /* iterate over reserved VM IDs and check no such nodes exist */
    for (size_t i = PG_VM_ID_BASE; i < PG_VM_ID_OFFSET; i++) {
        uint16_t        vm_id   = (uint16_t) i - PG_VM_ID_BASE;
        struct fdt_node vm_node = hyp_node;

        generate_vm_node_name(&vm_name, vm_id);
        RET(fdt_find_child(&vm_node, &vm_name), MANIFEST_ERROR_RESERVED_VM_ID,
            "detected use of reseved VM ID in manifest: %lu\n", vm_id);
    }

    /* iterate over VM nodes until we find one that does not exist */
    for (size_t i = 0; i <= MAX_VMS; i++) {
        uint16_t        vm_id   = PG_VM_ID_OFFSET + i;
        struct fdt_node vm_node = hyp_node;

        generate_vm_node_name(&vm_name, vm_id - PG_VM_ID_BASE);
        if (!fdt_find_child(&vm_node, &vm_name)) {
            break;
        }

        RET(i == MAX_VMS, MANIFEST_ERROR_TOO_MANY_VMS,
            "exceeded maximum number of VMs: %lu\n", MAX_VMS);

        if (vm_id == PG_PRIMARY_VM_ID) {
            DIE(found_primary_vm == true, "multiple primary VMs detected\n");
            found_primary_vm = true;
        }

        manifest->vm[i].vm = NULL;
        manifest->vm_count = i + 1;

        ans = parse_vm(&vm_node, &manifest->vm[i], vm_id);
        RET(ans != MANIFEST_SUCCESS, ans,
            "unable to parse VM %x manifest (%s)\n",
            i, manifest_strerror(ans));
    }

    RET(!found_primary_vm, MANIFEST_ERROR_NO_PRIMARY_VM,
         "no primary VM detected\n");

    return MANIFEST_SUCCESS;
}
