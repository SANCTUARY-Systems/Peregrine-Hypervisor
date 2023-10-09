#pragma once

#include "pg/arch/virt_devs.h"  /* struct virt_dev        */
#include "pg/arch/types.h"      /* arch-specific typedefs */

/* virtual device is enabled based on exports from
 * `project/sanctuary/BUILD.gn` and `build/toolchain/embedded.gni`
 *  @V_ANATOP_BASE : base address of virtual devices (default: 0)
 *  @V_ANATOP_SIZE : size of a single virtual device (default: 0)
 *  @V_ANATOP_DEVS : number of virtual devices       (default: 0)
 *
 *  @V_ANATOP_DIRMAP : map device directly to VM memory
 *                     predicated by device passthrough
 *
 *  @V_ANATOP_ENABLED : defined if virt device is enabled
 *  @V_ANATOP_PASSTHR : defined if device access passthrough intended
 *
 * NOTE: if base address and device size are defined, device is enabled
 *       if device is enabled but num of devices is 0, passthrough intended
 * NOTE: default values of 0 required because code is compiled in regardless
 *       of enabled state
 */
#if defined(V_ANATOP_BASE) && defined(V_ANATOP_SIZE)
#define V_ANATOP_ENABLED
#endif

#ifndef V_ANATOP_BASE
#define V_ANATOP_BASE 0
#endif

#ifndef V_ANATOP_SIZE
#define V_ANATOP_SIZE 0
#endif

#ifndef V_ANATOP_DEVS
#define V_ANATOP_DEVS 0
#endif

#if defined(V_ANATOP_ENABLED) && V_ANATOP_DEVS == 0
#define V_ANATOP_PASSTHR
#endif

#if defined(V_ANATOP_DIRMAP) && !defined(V_ANATOP_PASSTHR)
#undef V_ANATOP_DIRMAP
#endif

/* public API */
int64_t virt_anatop_init(struct virt_dev *, size_t);

