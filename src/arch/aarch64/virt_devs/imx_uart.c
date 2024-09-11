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

#include "pg/arch/virt_devs/imx_uart.h" /* virt imx uart API        */
#include "pg/arch/std.h"                /* memset_peregrine           */
#include "pg/arch/types.h"              /* arch-specific typedef    */
#include "pg/spinlock.h"                /* spinlocks                */
#include "pg/mm.h"                      /* MM_MODE_{R,W}            */
#include "pg/vm.h"                      /* vm_lock, vm_identity_map */
#include "pg/arch/init.h"               /* get_ppool                */
#include "pg/dlog.h"                    /* logging (no, really)     */

/******************************************************************************
 ************************* INTERNAL MACRO DEFINITIONS *************************
 ******************************************************************************/

/* config macros */
#define TX_BUF_SZ   2048    /* per-device Tx buffer size */

/* register definitions */
#define URXD0       0x00    /* Receiver Register                    */
#define URTX0       0x40    /* Transmitter Register                 */
#define UCR1        0x80    /* Control Register 1                   */
#define UCR2        0x84    /* Control Register 2                   */
#define UCR3        0x88    /* Control Register 3                   */
#define UCR4        0x8c    /* Control Register 4                   */
#define UFCR        0x90    /* FIFO Control Register                */
#define USR1        0x94    /* Status Register 1                    */
#define USR2        0x98    /* Status Register 2                    */
#define UESC        0x9c    /* Escape Character Register            */
#define UTIM        0xa0    /* Escape Timer Register                */
#define UBIR        0xa4    /* BRM Incremental Register             */
#define UBMR        0xa8    /* BRM Modulator Register               */
#define UBRC        0xac    /* Baud Rate Count Register             */
#define IMX21_ONEMS 0xb0    /* One Millisecond register             */
#define IMX1_UTS    0xd0    /* UART Test Register on i.mx1          */
#define IMX21_UTS   0xb4    /* UART Test Register on all other i.mx */

/* bit fields definitions */
#define UTS_TXEMPTY     (1<<6)  /* TxFIFO empty */
#define UTS_RXEMPTY     (1<<5)  /* RxFIFO empty */
#define UTS_TXFULL      (1<<4)  /* TxFIFO full */
#define UTS_RXFULL      (1<<3)  /* RxFIFO full */

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

/* backend API */
static void (*backing_putchar)(char);

/* register state (per virt device) */
static uint8_t dev_regs[V_IMX_UART_DEVS][V_IMX_UART_SIZE];

/* register state spinlock (per virt device) */
static struct spinlock spinlocks[MAX(V_IMX_UART_DEVS, 1)];

/* string buffers (Tx only) */
static char   tx_buff[V_IMX_UART_DEVS][TX_BUF_SZ];
static size_t tx_head[V_IMX_UART_DEVS];

/******************************************************************************
 ************************* INTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* reg_offset_to_name - converts offset into device memory to register name
 *  @offset : offset into device memory of accessed register
 *
 *  @return : ptr to register name
 */
static const char *
reg_offset_to_name(uint64_t offset)
{
    switch (offset) {
        case URXD0:       return "URXD0";
        case URTX0:       return "URTX0";
        case UCR1:        return "UCR1";
        case UCR2:        return "UCR2";
        case UCR3:        return "UCR3";
        case UCR4:        return "UCR4";
        case UFCR:        return "UFCR";
        case USR1:        return "USR1";
        case USR2:        return "USR2";
        case UESC:        return "UESC";
        case UTIM:        return "UTIM";
        case UBIR:        return "UBIR";
        case UBMR:        return "UBMR";
        case UBRC:        return "UBRC";
        case IMX21_ONEMS: return "IMX21_ONEMS";
        case IMX1_UTS:    return "IMX1_UTS";
        case IMX21_UTS:   return "IMX21_UTS";
        default:          return "unknown";
    }
}

/* emu_state_reset - resets emulateor state
 *  @minor : vierual device instance ID
 */
static void
emu_state_reset(uint8_t minor)
{
    /* clear out register state buffer */
    memset_peregrine(dev_regs[minor], 0x00, V_IMX_UART_SIZE);

    /* mark Tx FIFO empty */
    dev_regs[minor][IMX21_UTS] |= UTS_TXEMPTY;
    dev_regs[minor][IMX21_UTS] &= ~UTS_TXFULL;

    /* reset Tx buffer head */
    tx_head[minor] = 0;
}

/* emu_state_update - updates emulator state based on new memory operation
 *  @mode   : memory access type -- MM_MODE_{R,W}
 *  @offset : memory access offset
 *  @sas    : Syndrome Access Size (:2 bitfield)
 *  @minor  : virtual device instance ID
 */
static void
emu_state_update(uint32_t mode,
                 uint64_t offset,
                 uint8_t  sas,
                 uint8_t  minor)
{
    /* go to read/write specific sections */
    if (mode == MM_MODE_R) {
        goto op_read;
    }

    /* write operation state transition */
    switch (offset) {
        case URTX0:     /* Tx register -- putchar */
            /* buffer newly written character */
            tx_buff[minor][tx_head[minor]++] = dev_regs[minor][URTX0];

            /* flush buffer to backing device under certain conditions */
            if (dev_regs[minor][URTX0] == '\n'
            ||  tx_head[minor] == TX_BUF_SZ)
            {
                /* dick move; debugging purposes */
                dlog("virt-uart[%u]: ", minor);

                for (size_t i = 0; i < tx_head[minor]; i++) {
                    backing_putchar(tx_buff[minor][i]);
                }

                tx_head[minor] = 0;
            }

            break;
    }

    return;

op_read:
    /* read operation state transition */
    return;
}

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
#ifdef V_IMX_UART_DIRMAP
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

    /* log the access */
    dlog_debug("accessed vDev | "
              "name:%s minor:%u mode=%c%c "
              "SAS=%x SRT=%x SF=%x "
              "IPA:%#x offset:%#x reg:%s\n",
        dev->name, dev->minor,
        info->mode & MM_MODE_R ? 'r' : '-',
        info->mode & MM_MODE_W ? 'w' : '-',
        sas, srt, sf, info->ipaddr.ipa, offset,
        reg_offset_to_name(offset));

    /* if we want to map this device to a certain VM, this is where we do it *
     * we're basically doing it on demand in case some VMs start later       *
     * TODO: add some manifest checks once we include that information       *
     *       we don't want devices to be mapped to greedy VMs with platform  *
     *       knowledge (despite incomplete device trees)                     */
#ifdef V_IMX_UART_DIRMAP
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

    dlog_debug("Mapped device %s:%u to VM %#x\n",
               dev->name, dev->minor, vcpu->vm->id);

    vm_unlock(&vm_locked);

    /* do not increment PC; let the VM retry the same operation */
    return true;
#endif /* V_IMX_UART_DIRMAP */

    /* initialize easy access site pointers depending on access policy *
     *  - passhtrough  : relative to device base address               *
     *  - !passhtrough : relative to register state buffer             */
#ifdef V_IMX_UART_PASSTHR
    site_8  = (uint8_t  *) info->ipaddr.ipa;
    site_16 = (uint16_t *) info->ipaddr.ipa;
    site_32 = (uint32_t *) info->ipaddr.ipa;
    site_64 = (uint64_t *) info->ipaddr.ipa;
#else
    site_8  = (uint8_t  *) &dev_regs[dev->minor][offset];
    site_16 = (uint16_t *) &dev_regs[dev->minor][offset];
    site_32 = (uint32_t *) &dev_regs[dev->minor][offset];
    site_64 = (uint64_t *) &dev_regs[dev->minor][offset];
#endif /* V_IMX_UART_PASSTHR */

    /* perform memory operation on behalf of VM */
    sl_lock(&spinlocks[dev->minor]);

    switch (info->mode) {
        case MM_MODE_R:
            /* clear contents of dst register based on SF */
            vcpu->regs.r[srt] &= !sf * ~((1UL << 32) - 1);

#ifndef V_IMX_UART_PASSTHR
            /* emulation RO register whitelist */
            switch (offset) {
                case IMX21_UTS:     /* test register */
                    break;
                default:
                    dlog_warning("Register %s not implemented\n",
                        reg_offset_to_name(offset));
                    goto memop_abort;
            }
#endif /* V_IMX_UART_PASSTHR */

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

#ifndef V_IMX_UART_PASSTHR
            /* update emulator state based on newly performed operation */
            emu_state_update(info->mode, offset, sas, dev->minor);
#endif /* V_IMX_UART_PASSTHR */

            break;
        case MM_MODE_W:
#ifndef V_IMX_UART_PASSTHR
            /* emulation WO register whitelist */
            switch (offset) {
                case URTX0:     /* Tx register */
                    break;
                default:
                    dlog_warning("Register %s not implemented\n",
                        reg_offset_to_name(offset));
                    goto memop_abort;
            }
#endif /* V_IMX_UART_PASSTHR */

            /* corner case: SRT = 0b11111 means clear memory */
            switch (sas) {
                case 0x0:
                    *site_8  = (srt != 0x1f) * vcpu->regs.r[srt];
                    break;
                case 0x1:
                    *site_16 = (srt != 0x1f) * vcpu->regs.r[srt];
                    break;
                case 0x2:
                    *site_32 = (srt != 0x1f) * vcpu->regs.r[srt];
                    break;
                case 0x3:
                    *site_64 = (srt != 0x1f) * vcpu->regs.r[srt];
                    break;
                default:
                    dlog_error("BUG: Impossible SAS value (%#x)", sas);
                    ret = false;
            }

#ifndef V_IMX_UART_PASSTHR
            /* update emulator state based on newly performed operation */
            emu_state_update(info->mode, offset, sas, dev->minor);
#endif /* V_IMX_UART_PASSTHR */

            break;
        default:
            dlog_error("Unkown memory access type\n");
            ret = false;
    }

memop_abort:
    sl_unlock(&spinlocks[dev->minor]);

out:
    vcpu->regs.pc += pc_inc;
    return ret;
}

/* virt_imx_uart_init - initializes virtual imx uart device
 *  @dev              : ptr to head of virtual devices buffer
 *  @slots_left       : empty virtual device descritor slots in buffer
 *  @_backing_putchar : backing device putchar function
 *
 *  @return : number of devices added; -1 on error
 *
 * NOTE: we assume that the backing device has already been initialized
 */
int64_t
virt_imx_uart_init(struct virt_dev *dev,
                   size_t          slots_left,
                   void (*_backing_putchar)(char))
{
    dlog_debug("    + imx_uart\n");

    /* sanity check */
    if (V_IMX_UART_DEVS > slots_left || !slots_left) {
        dlog_error("Insufficient virt device descriptor slots remaining\n");
        return -1;
    }

    /* initialize backend */
    backing_putchar = _backing_putchar;

    /* register each device instance */
    for (size_t i = 0; i < V_IMX_UART_DEVS; i++) {
        /* initialize emulator's internal state */
        emu_state_reset(i);

        /* initialize spinlock */
        spinlocks[i] = SPINLOCK_INIT;

        /* initialize virtual device descriptor */
        dev[i].name       = "virt-imx_uart";
        dev[i].minor      = i;
        dev[i].addr_start = V_IMX_UART_BASE + i * V_IMX_UART_SIZE;
        dev[i].addr_end   = V_IMX_UART_BASE + (i + 1) * V_IMX_UART_SIZE;
        dev[i].access     = access;
    }

    /* or at least one device if we're doing passthrough */
#ifdef V_IMX_UART_PASSTHR
    /* initialize spinlock */
    spinlocks[0] = SPINLOCK_INIT;

    /* initialize virtual device descriptor */
#ifdef V_IMX_UART_DIRMAP
    dev->name       = "virt-imx_uart[DM]";
#else
    dev->name       = "virt-imx_uart[PT]";
#endif /* V_IMX_UART_DIRMAP */
    dev->minor      = 0;
    dev->addr_start = V_IMX_UART_BASE;
    dev->addr_end   = V_IMX_UART_BASE + V_IMX_UART_SIZE;
    dev->access     = access;

    return 1;
#endif /* V_IMX_UART_PASSTHR */

    return V_IMX_UART_DEVS;
}

