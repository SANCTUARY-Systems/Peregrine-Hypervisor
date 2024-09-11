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

#include "pg/arch/virt_devs.h"  /* virt devices management    */
#include "pg/arch/types.h"      /* arch-specific typedefs     */
#include "pg/plat/console.h"    /* plat_console_{put,get}char */
#include "pg/vcpu.h"            /* vcpu structures            */
#include "pg/dlog.h"            /* logging                    */

/* APIs of supported virtual devices */
#include "pg/arch/virt_devs/sanct_uart.h"
#include "pg/arch/virt_devs/imx_uart.h"
#include "pg/arch/virt_devs/anatop.h"
#include "pg/arch/virt_devs/clock_ctrl.h"
#include "pg/arch/virt_devs/iomux.h"

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

/* registered virtual devices */
static struct virt_dev devs[128];
static uint64_t        active_devs = 0;

/******************************************************************************
 ************************* INTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* virt_identity_map - maps a (passthrough) virtual device to Peregrine addr space
 *  @base          : starting address of the device region
 *  @size          : size of the device region
 *  @stage1_locked : currently locked stage-1 page table of hypervisor
 *  @ppool         : memory pool
 *
 *  @return : 0 if everything went well; -1 otherwise
 */
static int
virt_identity_map(uint64_t                base,
                  uint64_t                size,
                  struct mm_stage1_locked stage1_locked,
                  struct mpool            *ppool)
{
    if (!mm_identity_map(stage1_locked,
                         pa_init(base),
                         pa_init(base + size),
                         MM_MODE_R | MM_MODE_W | MM_MODE_D,
                         ppool))
    {
        dlog_error("Unable to map device to Peregrine address space\n");
        return -1;
    }

    return 0;
}

/******************************************************************************
 ********************************* PUBLIC API *********************************
 ******************************************************************************/

/* init_backing_devs - initialization of backing (real) devices
 *  @stage1_locked : currently locked stage-1 page table of hypervisor
 *  @ppool         : memory pool
 *
 *  @return : 0 if everything went well; -1 otherwise
 *
 * some devices like the UART are initialized and mapped to Peregrine's page table
 * early on. if we decide to use these as backing devices, everything will work
 * out.
 *
 * other devices such as the anatop power regulator are never really intialized.
 * trying to use them as passthroughs will lead to a Data Abort w/o change in
 * EL. due to some other problems with Peregrine, `arch_regs_reset()` will end up
 * dereferencing a NULL pointer (since vcpu->vm will be NULL for the hypervisor)
 * which in turn will cause a panic.
 *
 * this function is for initializing those backing devices.
 *
 * NOTE: if we ever want to map devices directly to the VMs, not that we can't
 *       do both `mm_identity_map()` and `vm_identity_map()`. need to look
 *       some more into that, at some point.
 */
int
init_backing_devs(struct mm_stage1_locked stage1_locked,
                  struct mpool            *ppool)
{
    static bool accessed = false;   /* was this function accessed previously? */

    /* prevent calling this function more than once              *
     * NOTE: secondary VMs currently use the same load procedure *
     *       as primary VMs. This will cause this function to be *
     *       called more than once, which is incorrect. While    *
     *       the secondary VM load path remains unchanged, keep  *
     *       this check here.                                    */
    if (accessed) {
        return 0;
    }
    accessed = true;

    dlog_debug("Initializing backing devices for virtual instances\n");

#if defined(V_ANATOP_ENABLED) && !defined(V_ANATOP_DIRMAP)
    dlog_debug("    + anatop\n");

    if (virt_identity_map(
            V_ANATOP_BASE, V_ANATOP_SIZE, stage1_locked, ppool))
    {
        dlog_error("Unable to initialize physical anatop device\n");
        return -1;
    }
#endif /* V_ANATOP_ENABLED && !V_ANATOP_DIRMAP */
#if defined(V_IOMUX_ENABLED) && !defined(V_IOMUX_DIRMAP)
    dlog_debug("    + iomux\n");

    if (virt_identity_map(
            V_IOMUX_BASE, V_IOMUX_SIZE, stage1_locked, ppool))
    {
        dlog_error("Unable to initialize physical pin controller\n");
        return -1;
    }
#endif /* V_IOMUX_ENABLED && !V_IOMUX_DIRMAP */
#if defined(V_CLOCK_CTRL_ENABLED) && !defined(V_CLOCK_CTRL_DIRMAP)
    dlog_debug("    + clock-ctrl\n");

    if (virt_identity_map(
            V_CLOCK_CTRL_BASE, V_CLOCK_CTRL_SIZE, stage1_locked, ppool))
    {
        dlog_error("Unable to initialize physical clock controller\n");
        return -1;
    }
#endif /* V_CLOCK_CTRL_ENABLED && !V_CLOCK_CTRL_DIRMAP */

    return 0;
}

/* init_virt_devs - initialization of virtual devices
 *  @return : 0 if everything went well; -1 otherwise
 */
int
init_virt_devs(void)
{
    static bool accessed = false;   /* was this function accessed previously? */
    int64_t     ans;                /* answer                                 */
    #pragma unused(ans)

    /* prevent calling this function more than once              *
     * NOTE: secondary VMs currently use the same load procedure *
     *       as primary VMs. This will cause this function to be *
     *       called more than once, which is incorrect. While    *
     *       the secondary VM load path remains unchanged, keep  *
     *       this check here.                                    */
    if (accessed) {
        return 0;
    }
    accessed = true;


    dlog_debug("Initializing virtual device instances\n");

#ifdef V_SANCT_UART_ENABLED
    ans = virt_sanct_uart_init(
                &devs[active_devs],
                sizeof(devs) / sizeof(*devs) - active_devs);
    if (ans < 0) {
        dlog_error("Unable to initialize sanctuary uart virtual dev(s)\n");
        return -1;
    }

    active_devs += ans;
#endif
#ifdef V_IMX_UART_ENABLED
    ans = virt_imx_uart_init(
                &devs[active_devs],
                sizeof(devs) / sizeof(*devs) - active_devs,
                plat_console_putchar);
    if (ans < 0) {
        dlog_error("Unable to initialize imx uart virtual dev(s)\n");
        return -1;
    }

    active_devs += ans;
#endif /* V_IMX_UART_ENABLED */
#ifdef V_ANATOP_ENABLED
    ans = virt_anatop_init(
            &devs[active_devs],
            sizeof(devs) / sizeof(*devs) - active_devs);
    if (ans < 0) {
        dlog_error("Unable to initialize anatop virtual dev(s)\n");
        return -1;
    }

    active_devs += ans;
#endif /* V_ANATOP_ENABLED */
#ifdef V_IOMUX_ENABLED
    ans = virt_iomux_init(
                &devs[active_devs],
                sizeof(devs) / sizeof(*devs) - active_devs);
    if (ans < 0) {
        dlog_error("Unable to initialize imx uart virtual dev(s)\n");
        return -1;
    }

    active_devs += ans;
#endif /* V_IOMUX_ENABLED */
#ifdef V_CLOCK_CTRL_ENABLED
    ans = virt_clock_ctrl_init(
                &devs[active_devs],
                sizeof(devs) / sizeof(*devs) - active_devs);
    if (ans < 0) {
        dlog_error("Unable to initialize imx uart virtual dev(s)\n");
        return -1;
    }

    active_devs += ans;
#endif /* V_CLOCK_CTRL_ENABLED */

    /* output all instances of registered devices */
    dlog_debug("Active virtual device instances\n");
    for (size_t i = 0; i < active_devs; i++) {
        dlog_debug(" ┌─ name       : %s\n",  devs[i].name);
        dlog_debug(" ├─ minor      : %u\n",  devs[i].minor);
        dlog_debug(" ├─ addr_start : %#x\n", devs[i].addr_start);
        dlog_debug(" └─ addr_end   : %#x\n", devs[i].addr_end);
    }

    return 0;
}

/* access_virt_dev - tries to handle memory access fault by VM
 *  @esr    : contents of ESR register (including fault reason)
 *  @far    : contents of FAR register (faulting address)
 *  @pc_inc : instruction length (if we want to increment PC)
 *  @vcpu   : vcpu that caused the fault
 *  @info   : extra information regarding the fault
 *
 *  @return : true if handled, false otherwise
 */
bool
access_virt_dev(uintreg_t               esr,
                uintreg_t               far,
                uint8_t                 pc_inc,
                struct vcpu             *vcpu,
                struct vcpu_fault_info  *info)
{
    struct virt_dev *dev = devs;

    /* look for the device in charge of the accessed address */
    for (size_t i = 0; i < active_devs; i++, dev++) {
        if (info->ipaddr.ipa >= dev->addr_start
        &&  info->ipaddr.ipa <  dev->addr_end)
        {
            /* search succeeded; delegate to virt device driver */
            return dev->access(esr, far, pc_inc, vcpu, info, dev);
        }
    }

    /* search failed; let the VM deal with this fault itself */
    return false;
}

