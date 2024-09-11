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

#include "pg/arch/virt_devs/sanct_uart.h"
#include "pg/arch/std.h"                /* memset_peregrine           */
#include "pg/arch/types.h"              /* arch-specific typedef    */
#include "pg/spinlock.h"                /* spinlocks                */
#include "pg/mm.h"                      /* MM_MODE_{R,W}            */
#include "pg/arch/init.h"               /* get_ppool                */
#include "pg/dlog.h"                    /* logging (no, really)     */
#include "pg/stdout.h"                  /* stdout_putchar           */

/******************************************************************************
 **************************** INTERNAL DEFINITIONS ****************************
 ******************************************************************************/

/* device register offsets */
#define URX     0x00    /* Receiver Register    */
#define UTX     0x04    /* Transmitter Register */
#define UCR     0x08    /* Control Register     */

/* configs */
#define TX_BUFF_SZ  2048    /* per-device Tx buffer size */

/******************************************************************************
 **************************** INTERNAL STRUCTURES *****************************
 ******************************************************************************/

/* register state (per virtual device) */
static uint8_t dev_regs[V_SANCT_UART_DEVS][V_SANCT_UART_SIZE];

/* spinlocks (per virtual device) */
static struct spinlock spinlocks[V_SANCT_UART_DEVS];

/* string buffers */
static char   tx_buff[V_SANCT_UART_DEVS][TX_BUFF_SZ];
static size_t tx_head[V_SANCT_UART_DEVS];

/******************************************************************************
 ************************* INTERNAL HELPER FUNCTIONS **************************
 ******************************************************************************/

/* reg_offset_to_name - converts offset into device memory to register name
 *  @offset : offset into device memory of accessed register
 *
 *  @return : ptr to register name
 */
__attribute__((unused))
static const char *
reg_offset_to_name(uint64_t offset)
{
    switch (offset) {
        case URX:   return "URX";
        case UTX:   return "UTX";
        case UCR:   return "UCR";
        default:    return "unknown";
    }
}

/* stdout_puts - dlog bypass for raw string write to stdout
 *  @buff : buffer to print (must be '\0' terminated if size = 0)
 *  @size : number of characters to print or
 *          0 to print up until '\0'
 *
 *  @return : number of printed characters
 *
 * NOTE: this function is not subject to the dlog lock; make sure you have
 *       acquired it beforehand!
 */
static size_t
stdout_puts(char *buff, size_t size)
{
    size_t count = 0;   /* number of printed characters */

    for (char *p = buff; size ? p < buff + size : *p; p++, count++) {
        stdout_putchar(*p);
    }

    return count;
}

/* flush_tx_buff - flushes a certain Tx buffer to the real uart
 *  @minor : device instance identifier
 *
 * I know that there's already a dlog_flush_vm_buffer() implemented but it's
 * too annoying to use; has per-VM tags (not per-device) and automatically
 * prints a '\n' after the given output.
 */
static void
flush_tx_buff(uint8_t minor)
{
    static char lpc = '\n';     /* last printed character */

    /* don't even bother if Tx buffer is empty */
    if (tx_head[minor] == 0) {
        return;
    }

    /* dlog lock syncrhonizes writes between all VMs & hypervisor *
     * but not OP-TEE; some junk is bound to appear this way      */
    dlog_lock();

    /* print tag only if this is the start of a new line */
    if (lpc == '\n') {
        stdout_puts("sanct-uart[", 0);
        stdout_putchar('0' + minor);
        stdout_puts("]: ", 0);
    }

    /* print the actual buffer contents - bypass dlog bloat */
    stdout_puts(tx_buff[minor], tx_head[minor]);

    lpc = tx_buff[minor][tx_head[minor] - 1];
    tx_head[minor] = 0;

    dlog_unlock();
}

/* dev_reset - resets the internal state of the device
 *  @minor : device instance identifier
 */
static void
dev_reset(uint8_t minor)
{
    /* register state */
    *((sanct_urx_t *) &dev_regs[minor][URX]) = (sanct_urx_t) { 0 };
    *((sanct_utx_t *) &dev_regs[minor][UTX]) = (sanct_utx_t) { 0 };
    *((sanct_ucr_t *) &dev_regs[minor][UCR]) = (sanct_ucr_t) { 0 };

    /* buffers */
    tx_head[minor] = 0;
}

/* dev_read - performs a read operation on behalf of the VM
 *  @offset : register offset into virtual device region
 *  @minor  : device instance identifier
 *  @sas    : syndrome access size
 *
 *  @return : read value
 */
static uint64_t
dev_read(uint64_t offset,
         uint8_t  minor,
         uint8_t  sas)
{
    uint64_t value = 0; /* read value */
    uint64_t mask;      /* SAS mask   */

    /* sanity check */
    ALERT(sas != 2, "Read with invalid SAS: %#x\n", sas);

    /* perform register specific read operation */
    switch (offset) {
        case URX:   /* Receiver Register */
            value = *((uint32_t *) &dev_regs[minor][offset]);
            break;
    }

    /* make sure the returned value size corresponds to SAS */
    mask = -1UL >> (64 - (1 << (3 + sas)));
    return value & mask;
}

/* dev_write - performs a write operation on behalf of the VM
 *  @value  : value to be written
 *  @offset : register offset into virtual device region
 *  @minor  : device instance identifier
 *  @sas    : syndrome access size
 */
static void
dev_write(uint64_t value,
          uint64_t offset,
          uint8_t  minor,
          uint8_t  sas)
{
    sanct_utx_t *utx_reg;   /* easy access UTX reg */
    sanct_ucr_t *ucr_reg;   /* easy access UCR reg */

    /* sanity check */
    ALERT(sas != 2, "Write with invalid SAS: %#x\n", sas);

    /* perform register specific write operation */
    switch (offset) {
        case UTX:   /* Transmitter Register */
            utx_reg = (sanct_utx_t *) &value;

            tx_buff[minor][tx_head[minor]++] = utx_reg->TX_DATA;

            /* flush on '\n' or buffer fill */
            if (tx_head[minor] == TX_BUFF_SZ
            ||  utx_reg->TX_DATA == 0x0AU) // '\n'
            {
                flush_tx_buff(minor);
            }

            break;
        case UCR:   /* Control Register */
            ucr_reg = (sanct_ucr_t *) &value;

            /* VM requested buffer flush                                *
             * NOTE: this is done every time a tty string tx operation  *
             *       finishes (subject to change later on); beware that *
             *       it is _very_ likely that a string print could be   *
             *       split into two tty writes, one containing the bulk *
             *       of the string and the other containing "\r\n",     *
             *       translated from a single '\n'                      */
            if (ucr_reg->TX_FLUSH) {
                flush_tx_buff(minor);
            }

            break;
    }
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
    uint64_t offset;      /* access offset into device region        */
    uint8_t  isv;         /* Instruction Syndrome Valid              */
    uint8_t  sas;         /* Syndrome Access Size                    */
    uint8_t  srt;         /* Syndrome Register Transfer              */
    uint8_t  sf;          /* Sixty Four                              */
    bool     ret = false; /* success state of this function          */

    /* calculate accessed offset */
    offset = info->ipaddr.ipa - dev->addr_start;

    /* extract ISV, SAS, SRT, SF from ESR */
    isv = (esr >> 24) & 0x1;
    sas = (esr >> 22) & 0x3;
    srt = (esr >> 16) & 0x1f;
    sf  = (esr >> 15) & 0x1;

    /* sanity check for ISV (unlikely)                 *
     * NOTE: if ISV is not set, SAS/SRT/SF are unknown */
    GOTO(unlikely(!isv), out, "Invalid Instruction Syndrome\n");

    /* log the access */
    /* dlog_debug("accessed vDev | " */
    /*           "name:%s minor:%u mode=%c%c " */
    /*           "SAS=%x SRT=%x SF=%x " */
    /*           "IPA:%#x offset:%#x reg:%s\n", */
    /*     dev->name, dev->minor, */
    /*     info->mode & MM_MODE_R ? 'r' : '-', */
    /*     info->mode & MM_MODE_W ? 'w' : '-', */
    /*     sas, srt, sf, info->ipaddr.ipa, offset, */
    /*     reg_offset_to_name(offset)); */

    /* resolve memory operation on behalf of VM *
     * ensure exclusive access for SMP VMs      */
    sl_lock(&spinlocks[dev->minor]);

    switch(info->mode) {
        case MM_MODE_R:
            /* clear contents of destination register based on SF   *
             * update it with information generated by read handler */
            vcpu->regs.r[srt] &= !sf * ~((1UL << 32) - 1);
            vcpu->regs.r[srt] |= dev_read(offset, dev->minor, sas);

            break;
        case MM_MODE_W:
            /* perform device write operation                *
             * corner case: SRT = 0b11111 means clear memory */
            dev_write((srt != 0x1f) * vcpu->regs.r[srt],
                      offset, dev->minor, sas);

            break;
        default:
            GOTO(1, op_abort, "Unknown memory access type: %#x\n", info->mode);
    }

    /* memory operation performed successfully */
    ret = true;

op_abort:
    sl_unlock(&spinlocks[dev->minor]);

out:
    vcpu->regs.pc += pc_inc;
    return ret;
}

/* virt_sanct_uart_init - initializes virtual imx uart device
 *  @dev         : ptr to head of virtual device
 *  @slots_left  : empty virtual device descriptor slots in buffer
 *
 *  @return : number of devices added; -1 on error
 */
int64_t
virt_sanct_uart_init(struct virt_dev *dev,
                     size_t          slots_left)
{
    dlog_debug("    + sanct_uart\n");

    /* sanity check */
    RET(V_SANCT_UART_DEVS > slots_left, -1,
        "Insufficient virtual device descriptor slots\n");

    /* register each device instance */
    for (size_t i = 0; i < V_SANCT_UART_DEVS; i++) {
        /* reset device state */
        dev_reset(i);

        /* initialize spinlock */
        spinlocks[i] = SPINLOCK_INIT;

        /* initialize virtual device descriptor */
        dev[i].name       = "virt-sanct_uart";
        dev[i].minor      = i;
        dev[i].addr_start = V_SANCT_UART_BASE + i * V_SANCT_UART_SIZE;
        dev[i].addr_end   = V_SANCT_UART_BASE + (i + 1) * V_SANCT_UART_SIZE;
        dev[i].access     = access;
    }

    /* enable dlog serial locking */
    dlog_enable_lock();

    return V_SANCT_UART_DEVS;
}

