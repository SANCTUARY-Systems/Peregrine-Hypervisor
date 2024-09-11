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

#include "pg/io.h"
#include "pg/mm.h"
#include "pg/mpool.h"
#include "pg/plat/console.h"

/* register definitions */
#define URXD0       0x00    /* Receiver Register         */
#define URTX0       0x40    /* Transmitter Register      */
#define UCR1        0x80    /* Control Register 1        */
#define UCR2        0x84    /* Control Register 2        */
#define UCR3        0x88    /* Control Register 3        */
#define UCR4        0x8c    /* Control Register 4        */
#define UFCR        0x90    /* FIFO Control Register     */
#define USR1        0x94    /* Status Register 1         */
#define USR2        0x98    /* Status Register 2         */
#define UESC        0x9c    /* Escape Character Register */
#define UTIM        0xa0    /* Escape Timer Register     */
#define UBIR        0xa4    /* BRM Incremental Register  */
#define UBMR        0xa8    /* BRM Modulator Register    */
#define UBRC        0xac    /* Baud Rate Count Register  */
#define IMX_ONEMS   0xb0    /* One Millisecond register  */
#define IMX_UTS     0xb4    /* UART Test Register        */

/* register bitfields */
#define UTS_TXEMPTY (1<<6)  /* TxFIFO empty */
#define UTS_RXEMPTY (1<<5)  /* RxFIFO empty */
#define UTS_TXFULL  (1<<4)  /* TxFIFO full */
#define UTS_RXFULL  (1<<3)  /* RxFIFO full */

/******************************************************************************
 ************************* PUBLIC API IMPLEMENTATION **************************
 ******************************************************************************/

/* plat_console_init - initalizes the console hardware
 */
void
plat_console_init(void)
{
    /* hope for the best */
}

/* plat_console_mm_init - initializes any memory mappings that the driver needs
 *  @stage1_locked : stage-1 page table of the hypervisor
 *  @ppool         : available memory pool
 */
void
plat_console_mm_init(struct mm_stage1_locked stage1_locked,
                     struct mpool            *ppool)
{
    /* map page for UART */
    mm_identity_map(stage1_locked,
                    pa_init(UART_BASE),
                    pa_add(pa_init(UART_BASE), PAGE_SIZE),
                    MM_MODE_R | MM_MODE_W | MM_MODE_D,
                    ppool);
}

/* plat_console_putchar - puts a single character on the console (blocking)
 *  @c : the character
 */
void
plat_console_putchar(char c)
{
    /* print a CR together with a NL */
    if (c == '\n') {
        plat_console_putchar('\r');
    }

    /* wait until there is room in the Tx buffer */
    while (*(uint32_t *)(UART_BASE + IMX_UTS) & UTS_TXFULL) {
        __asm__ volatile("nop");
    }

    /* write character; force memory access ordering */
    memory_ordering_barrier();
    *(uint32_t *)(UART_BASE + URTX0) = (uint32_t) c;
    memory_ordering_barrier();
}

/* plat_console_getchar - gets a single character from the console (blocking)
 *  @return : the character
 */
char
plat_console_getchar(void)
{
    /* wait until there is data in the Rx buffer */
    while (*(uint32_t *)(UART_BASE + IMX_UTS) & UTS_RXEMPTY) {
        __asm__ volatile("nop");
    }

    /* read character */
    return *(uint32_t *)(UART_BASE + URXD0) & 0xff;
}

