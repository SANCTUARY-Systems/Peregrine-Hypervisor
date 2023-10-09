#pragma once

#include "pg/arch/virt_devs.h"  /* struct virt_dev        */
#include "pg/arch/types.h"      /* arch-specific typedefs */

/* virtual device is enabled based on exports from
 * `project/sanctuary/BUILD.gn` and `build/toolchain/embedded.gni`
 *  @V_IOMUX_BASE : base address of virtual devices (default: 0)
 *  @V_IOMUX_SIZE : size of a single virtual device (default: 0)
 *  @V_IOMUX_DEVS : number of virtual devices       (default: 0)
 *
 *  @V_IOMUX_DIRMAP : map device directly to VM memory
 *                    predicated by device passthrough
 *
 *  @V_IOMUX_ENABLED : defined if virt device is enabled
 *  @V_IOMUX_PASSTHR : defined if device access passthrough intended
 *
 * NOTE: if base address and device size are defined, device is enabled
 *       if device is enabled but num of devices is 0, passthrough intended
 * NOTE: default values of 0 required because code is compiled in regardless
 *       of enabled state
 */
#if defined(V_IOMUX_BASE) && defined(V_IOMUX_SIZE)
#define V_IOMUX_ENABLED
#endif

#ifndef V_IOMUX_BASE
#define V_IOMUX_BASE 0
#endif

#ifndef V_IOMUX_SIZE
#define V_IOMUX_SIZE 0
#endif

#ifndef V_IOMUX_DEVS
#define V_IOMUX_DEVS 0
#endif

#if defined(V_IOMUX_ENABLED) && V_IOMUX_DEVS == 0
#define V_IOMUX_PASSTHR
#endif

#if defined(V_IOMUX_DIRMAP) && !defined(V_IOMUX_PASSTHR)
#undef V_IOMUX_DIRMAP
#endif

/* public API */
int64_t virt_iomux_init(struct virt_dev *, size_t);

