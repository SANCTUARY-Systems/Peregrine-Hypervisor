#include "pg/arch/virt_devs/clock_ctrl.h"   /* virt clock controller API */
#include "pg/arch/std.h"                    /* memset_peregrine            */
#include "pg/arch/types.h"                  /* arch-specific typedefs    */
#include "pg/spinlock.h"                    /* spinlocks                 */
#include "pg/mm.h"                          /* MM_MODE_{R,W}             */
#include "pg/vm.h"                          /* vm_lock, vm_identity_map  */
#include "pg/arch/init.h"                   /* get_ppool                 */
#include "pg/dlog.h"                        /* logging                   */

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

/* register state (per virt device) */
static uint8_t dev_regs[V_CLOCK_CTRL_DEVS][V_CLOCK_CTRL_SIZE];

/* register state spinlock (per virt device) */
static struct spinlock spinlocks[MAX(V_CLOCK_CTRL_DEVS, 1)];

/******************************************************************************
 ************************* PUBLIC API IMPLEMENTATION **************************
 ******************************************************************************/

/* access - handles VM memory access fault in dev_regs region
 *  @esr    : contents of ESR register (including fault reason)
 *  @far    : contents of FAR register (faulting address)
 *  @pc_inc : instruction length (if we want to increment PC)
 *  @vcpu   : vcpu that caused the fault
 *  @info   : extra information regarding the fault
 *  @dev    : matched virtual device descriptor
 *
 *  @return : true if handled, false otherwise
 *
 * NOTE: this should be called only via `virt_dev`
 */
static bool
access(uintreg_t              esr,
       uintreg_t              far,
       uint8_t                pc_inc,
       struct vcpu            *vcpu,
       struct vcpu_fault_info *info,
       struct virt_dev        *dev)
{
    uint64_t         offset = info->ipaddr.ipa - dev->addr_start;
    uint8_t          isv;        /* Instruction Syndrome Valid              */
    uint8_t          sas;        /* Syndrome Access Size                    */
    uint8_t          srt;        /* Syndrome Register Transfer              */
    uint8_t          sf;         /* Sixty Four                              */
    uint8_t          *site_8;    /* ptr to byte-sized accessed memory       */
    uint16_t         *site_16;   /* ptr to halfword-sized accessed memory   */
    uint32_t         *site_32;   /* ptr to word-sized accessed memory       */
    uint64_t         *site_64;   /* ptr to doubleword-sized accessed memory */
    bool             ret = true; /* success state of this function          */
#ifdef V_CLOCK_CTRL_DIRMAP
    struct vm_locked vm_locked;  /* locked referene to faulting VM          */
#endif

    /* extract ISV, SAS, SRT, SF from ESR */
    isv = (esr >> 24) & 0x1;
    sas = (esr >> 22) & 0x3;
    srt = (esr >> 16) & 0x1f;
    sf  = (esr >> 15) & 0x1;

    /* sanity check for ISV (unlikely)                     *
     * NOTE: if ESR[24] is not set, SAS/SRT/SF are unknown */
    if (unlikely(!isv)) {
        dlog_error("Invalid Instruction Syndrome; potential bug\n");
        ret = false;
        goto out;
    }

    /* log the access                                           *
     * NOTE: see linux/include/dt-bindings/clock/imx8mm-clock.h */
    dlog_debug("accessed vDev | "
              "name:%s minor:%u mode=%c%c "
              "SAS=%x SRT=%x SF=%x "
              "IPA:%#x offset:%#x\n",
        dev->name, dev->minor,
        info->mode & MM_MODE_R ? 'r' : '-',
        info->mode & MM_MODE_W ? 'w' : '-',
        sas, srt, sf,
        info->ipaddr.ipa, offset);

    /* if we want to map this device to a certain VM, this is where we do it *
     * we're basically doing it on demand in case some VMs start later       *
     * TODO: add some manifest checks once we include that information       *
     *       we don't want devices to be mapped to greedy VMs with platform  *
     *       knowledge (despite incomplete device trees)                     */
#ifdef V_CLOCK_CTRL_DIRMAP
    /* sanity check; Peregrine can fault by accessing unmapped devices */
    if (!vcpu->vm) {
        dlog_error("Attempting to map device to no VM in particular (?)\n");
        ret = false;
        goto out;
    }

    /* map device in VM's address space */
    vm_locked = vm_lock(vcpu->vm);

    if (!vm_identity_map(vm_locked,
                         pa_init(dev->addr_start),
                         pa_init(dev->addr_end),
                         MM_MODE_R | MM_MODE_W,
                         get_ppool(), NULL))
    {
        dlog_error("Unable to map device to VM %#x\n", vcpu->vm->id);
        vm_unlock(&vm_locked);
        ret = false;
        goto out;
    }

    dlog_error("Mapped device %s:%u to VM %#x\n",
               dev->name, dev->minor, vcpu->vm->id);

    vm_unlock(&vm_locked);

    /* do not increment PC; let the VM retry the same operation */
    return true;
#endif /* V_CLOCK_CTRL_DIRMAP */

    /* initialize easy access site pointers depending on access policy *
     *  - passhtrough  : relative to device base address               *
     *  - !passhtrough : relative to register state buffer             */
#ifdef V_CLOCK_CTRL_PASSTHR
    site_8  = (uint8_t  *) info->ipaddr.ipa;
    site_16 = (uint16_t *) info->ipaddr.ipa;
    site_32 = (uint32_t *) info->ipaddr.ipa;
    site_64 = (uint64_t *) info->ipaddr.ipa;
#else
    /* fully virtual device not yet implemented */
    dlog_error("Only passthrough mode implemented!\n");
    ret = false;
    goto out;

    site_8  = (uint8_t  *) &dev_regs[dev->minor][offset];
    site_16 = (uint16_t *) &dev_regs[dev->minor][offset];
    site_32 = (uint32_t *) &dev_regs[dev->minor][offset];
    site_64 = (uint64_t *) &dev_regs[dev->minor][offset];
#endif /* V_CLOCK_CTRL_PASSTHR */

    /* filter access to specific registers */
    if (info->mode == MM_MODE_W
    && (offset == 0x44b0 || offset == 0xb000))
    {
        dlog_debug("blocked write to ccm[%#x]\n", offset);
        dlog_debug("ccm[%#x] = %#x\n", offset, *site_32);
        dlog_debug("value = %#x\n", vcpu->regs.r[srt]);

        ret = true;
        goto out;
    }

    /* perform memory operation on behalf of VM */
    sl_lock(&spinlocks[dev->minor]);

    switch (info->mode) {
        case MM_MODE_R:
            /* clear contents of dst register based on SF */
            vcpu->regs.r[srt] &= !sf * ~((1UL << 32) - 1);

            /* preserve higher half of whole register if dst is 32b */
            switch (sas) {
                case 0x0:
                    vcpu->regs.r[srt] |= *site_8;
                    break;
                case 0x1:
                    vcpu->regs.r[srt] |= *site_16;
                    break;
                case 0x2:
                    vcpu->regs.r[srt] |= *site_32;
                    break;
                case 0x3:
                    vcpu->regs.r[srt] |= *site_64;
                    break;
                default:
                    dlog_error("BUG: Impossible SAS value (%#x)", sas);
                    ret = false;
            }

            break;
        case MM_MODE_W:
            /* corner case: SRT = 0b11111 means clear memory */
            switch (sas) {
                case 0x0:
                    *site_8  = (srt != 0x1f) ? vcpu->regs.r[srt] : 0;
                    break;
                case 0x1:
                    *site_16 = (srt != 0x1f) ? vcpu->regs.r[srt] : 0;
                    break;
                case 0x2:
                    *site_32 = (srt != 0x1f) ? vcpu->regs.r[srt] : 0;
                    break;
                case 0x3:
                    *site_64 = (srt != 0x1f) ? vcpu->regs.r[srt] : 0;
                    break;
                default:
                    dlog_error("BUG: Impossible SAS value (%#x)", sas);
                    ret = false;
            }

            break;
        default:
            dlog_error("Unkown memory access type\n");
            ret = false;
    }

    sl_unlock(&spinlocks[dev->minor]);

out:
    vcpu->regs.pc += pc_inc;
    return ret;
}

/* virt_clock_ctrl_init - initializes virtual clock controller device
 *  @dev              : ptr to head of virtual devices buffer
 *  @slots_left       : empty virtual device descritor slots in buffer
 *
 *  @return : number of devices added; -1 on error
 *
 * NOTE: we assume that the backing device has already been initialized
 */
int64_t
virt_clock_ctrl_init(struct virt_dev *dev,
                     size_t          slots_left)
{
    dlog_debug("    + clock-ctrl\n");

    /* sanity check */
    if (V_CLOCK_CTRL_DEVS > slots_left || !slots_left) {
        dlog_error("Insufficient virt device descriptor slots remaining\n");
        return -1;
    }

    /* register each device instance */
    for (size_t i = 0; i < V_CLOCK_CTRL_DEVS; i++) {
        /* initialize internal register state */
        memset_peregrine(dev_regs[i], 0x00, V_CLOCK_CTRL_SIZE);

        /* initialize spinlock */
        spinlocks[i] = SPINLOCK_INIT;

        /* initialize virtual device descriptor */
        dev[i].name       = "virt-clock_ctrl";
        dev[i].minor      = i;
        dev[i].addr_start = V_CLOCK_CTRL_BASE + i * V_CLOCK_CTRL_SIZE;
        dev[i].addr_end   = V_CLOCK_CTRL_BASE + (i + 1) * V_CLOCK_CTRL_SIZE;
        dev[i].access     = access;
    }

    /* or at least one device if we're doing passthrough */
#ifdef V_CLOCK_CTRL_PASSTHR
    /* initialize spinlock */
    spinlocks[0] = SPINLOCK_INIT;

    /* initialize virtual device descriptor */
#ifdef V_CLOCK_CTRL_DIRMAP
    dev->name       = "virt-clock_ctrl[DM]";
#else
    dev->name       = "virt-clock_ctrl[PT]";
#endif /* V_CLOCK_CTRL_DIRMAP */
    dev->minor      = 0;
    dev->addr_start = V_CLOCK_CTRL_BASE;
    dev->addr_end   = V_CLOCK_CTRL_BASE + V_CLOCK_CTRL_SIZE;
    dev->access     = access;

    return 1;
#endif /* V_CLOCK_CTRL_PASSTHR */

    return V_CLOCK_CTRL_DEVS;
}

