#pragma once

#include "pg/arch/types.h"      /* arch-specific typedefs  */
#include "pg/vcpu.h"            /* vcpu structures         */
#include "pg/mm.h"              /* mm_stage1_locked        */
#include "pg/mpool.h"           /* mpool                   */

/* struct virt_dev - virtual device description
 *  @name       : name of virtual device
 *  @minor      : numeric identifier of virtual device instance
 *  @addr_start : starting address of internal register state
 *  @addr_end   : ending address of internal register state
 *  @access     : handler function for memory access
 *
 * NOTE: an instance of this generic interface must be returned by a
 *       device-specific setup function.
 */
struct virt_dev {
    const char  *name;
    uint8_t     minor;
    uintpaddr_t addr_start;
    uintpaddr_t addr_end;

    bool (*access)(uintreg_t, uintreg_t, uint8_t, struct vcpu *,
                   struct vcpu_fault_info *, struct virt_dev *);
};

/* public API */
int init_backing_devs(struct mm_stage1_locked, struct mpool *);
int init_virt_devs(void);
bool access_virt_dev(uintreg_t, uintreg_t, uint8_t, struct vcpu *,
                     struct vcpu_fault_info *);

/* used in many virt device driver sources *
 * this is the only common header (so far) */
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef likely
#define likely(cond) __builtin_expect(cond, 1)
#endif

#ifndef unlikely
#define unlikely(cond) __builtin_expect(cond, 0)
#endif

