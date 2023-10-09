#pragma once

#include "pg/arch/virt_devs.h"  /* struct virt_dev        */
#include "pg/arch/types.h"      /* arch-specific typedefs */

/* virtual device is enabled based on exports from
 * `project/sanctuary/BUILD.gn` and `build/toolchain/embedded.gni`
 *  @V_SANCT_UART_BASE : base address of virtual devices (default: 0)
 *  @V_SANCT_UART_SIZE : size of a single virtual device (default: 0)
 *  @V_SANCT_UART_DEVS : number of virtual devices       (default: 0)
 *
 *  @V_SANCT_UART_ENABLED : defined if virt device is enabled
 */
#if defined(V_SANCT_UART_BASE) && defined(V_SANCT_UART_SIZE)
#define V_SANCT_UART_ENABLED
#endif

#ifndef V_SANCT_UART_BASE
#define V_SANCT_UART_BASE 0
#endif

#ifndef V_SANCT_UART_SIZE
#define V_SANCT_UART_SIZE 0
#endif

#ifndef V_SANCT_UART_DEVS
#ifdef V_SANCT_UART_ENABLED
#define V_SANCT_UART_DEVS 1
#else
#define V_SANCT_UART_DEVS 0
#endif
#endif

/* device register structure                  *
 * NOTE: named from the perspective of the VM */
typedef union {
    struct {
        uint32_t RX_DATA   :  8;    /* Received Data */
        uint32_t reserved0 :  7;    /* Reserved      */
        uint32_t CHR_RDY   :  1;    /* Char Ready    */
        uint32_t reserved1 : 16;    /* Reserved      */
    };
    uint32_t raw;
} sanct_urx_t;

typedef union {
    struct {
        uint32_t TX_DATA   :  8;    /* Transmitted data */
        uint32_t reserved  : 24;    /* Reserved         */
    };
    uint32_t raw;
} sanct_utx_t;

typedef union {
    struct {
        uint32_t TX_FLUSH : 1;      /* Flush EL2 Tx buffer */
        uint32_t reserved : 31;     /* Reserved            */
    };
    uint32_t raw;
} sanct_ucr_t;

/* public API */
int64_t virt_sanct_uart_init(struct virt_dev *, size_t);

