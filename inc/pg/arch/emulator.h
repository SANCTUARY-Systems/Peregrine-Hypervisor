#pragma once

#include "pg/std.h"
#include "pg/vcpu.h"
#include "pg/mm.h"

#define MAX_INTERRUPTS 1024

/******************************************************************************
 ************************ GICD: DISTRIBUTOR REGISTERS *************************
 ******************************************************************************/

/* NOTE: pretty much required */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GICD_BASE
#define FB_GICD GICD_BASE
#endif

#ifdef GICD_SIZE
#define FB_GICD_SIZE GICD_SIZE
#endif

#if defined(FB_GICD) && defined(FB_GICD_SIZE)
#define GICD_ENABLED
#else
#if !defined(HOST_TESTING_MODE) || HOST_TESTING_MODE == 0
#pragma message ( "Undefined GICD stuff. Check project/sanctuary/BUILD.gn." )
#endif
#endif

/* register offsets */
#define FB_GICD_CTLR_OFFSET	0x0000
#define FB_GICD_TYPER_OFFSET	0x0004
#define FB_GICD_IIDR_OFFSET	0x0008
#define FB_GICD_TYPER2_OFFSET	0x000C
#define FB_GICD_STATUSR_OFFSET	0x0010
#define FB_GICD_SETSPI_NSR_OFFSET	0x0040
#define FB_GICD_CLRSPI_NSR_OFFSET	0x0048
#define FB_GICD_SETSPI_SR_OFFSET	0x0050
#define FB_GICD_CLRSPI_SR_OFFSET	0x0058
#define FB_GICD_IGROUPR0_OFFSET	0x0080 //+ (4 * n)
#define FB_GICD_ISENABLER0_OFFSET	0x0100 //+ (4 * n)
#define FB_GICD_ICENABLER0_OFFSET	0x0180 //+ (4 * n)
#define FB_GICD_ISPENDR0_OFFSET	0x0200 //+ (4 * n)
#define FB_GICD_ICPENDR0_OFFSET	0x0280 //+ (4 * n)
#define FB_GICD_ISACTIVER0_OFFSET	0x0300 //+ (4 * n)
#define FB_GICD_ICACTIVER0_OFFSET	0x0380 //+ (4 * n)
#define FB_GICD_IPRIORITYR0_OFFSET	0x0400 //+ (4 * n)
#define FB_GICD_ITARGETSR0_OFFSET	0x0800 //+ (4 * n)
#define FB_GICD_ICFGR0_OFFSET	0x0C00 //+ (4 * n)
#define FB_GICD_IGRPMODR0_OFFSET	0x0D00 //+ (4 * n)
#define FB_GICD_NSACR0_OFFSET	0x0E00 //+ (4 * n)
#define FB_GICD_SGIR_OFFSET	0x0F00
#define FB_GICD_CPENDSGIR0_OFFSET	0x0F10 //+ (4 * n)
#define FB_GICD_SPENDSGIR0_OFFSET	0x0F20 //+ (4 * n)
#define FB_GICD_INMIR0_OFFSET	0x0F80 //+ (4 * n)
#define FB_GICD_IGROUPR0E_OFFSET	0x1000 //+ (4 * n)
#define FB_GICD_ISENABLER0E_OFFSET	0x1200 //+ (4 * n)
#define FB_GICD_ICENABLER0E_OFFSET	0x1400 //+ (4 * n)
#define FB_GICD_ISPENDR0E_OFFSET	0x1600 //+ (4 * n)
#define FB_GICD_ICPENDR0E_OFFSET	0x1800 //+ (4 * n)
#define FB_GICD_ISACTIVER0E_OFFSET	0x1A00 //+ (4 * n)
#define FB_GICD_ICACTIVER0E_OFFSET	0x1C00 //+ (4 * n)
#define FB_GICD_IPRIORITYR0E_OFFSET	0x2000 //+ (4 * n)
#define FB_GICD_ICFGR0E_OFFSET	0x3000 //+ (4 * n)
#define FB_GICD_IGRPMODR0E_OFFSET	0x3400 //+ (4 * n)
#define FB_GICD_NSACR0E_OFFSET	0x3600 //+ (4 * n)
#define FB_GICD_INMIR0E_OFFSET	0x3B00 //+ (4 * n)
#define FB_GICD_IROUTER0_OFFSET	0x6000 //+ (8 * n)
#define FB_GICD_IROUTER0E_OFFSET	0x8000 //+ (8 * n)
#define FB_GICD_PIDR2_OFFSET	0xffe8

/* register addresses */
#define FB_GICD_CTLR	(FB_GICD + FB_GICD_CTLR_OFFSET)
#define FB_GICD_TYPER	(FB_GICD + FB_GICD_TYPER_OFFSET)
#define FB_GICD_IIDR	(FB_GICD + FB_GICD_IIDR_OFFSET)
#define FB_GICD_TYPER2	(FB_GICD + FB_GICD_TYPER2_OFFSET)
#define FB_GICD_STATUSR	(FB_GICD + FB_GICD_STATUSR_OFFSET)
#define FB_GICD_STATUSR	(FB_GICD + FB_GICD_STATUSR_OFFSET)
#define FB_GICD_SETSPI_NSR	(FB_GICD + FB_GICD_SETSPI_NSR_OFFSET)
#define FB_GICD_CLRSPI_NSR	(FB_GICD + FB_GICD_CLRSPI_NSR_OFFSET)
#define FB_GICD_SETSPI_SR	(FB_GICD + FB_GICD_SETSPI_SR_OFFSET)
#define FB_GICD_CLRSPI_SR	(FB_GICD + FB_GICD_CLRSPI_SR_OFFSET)
#define FB_GICD_IGROUPR0	(FB_GICD + FB_GICD_IGROUPR0_OFFSET)
#define FB_GICD_ISENABLER0	(FB_GICD + FB_GICD_ISENABLER0_OFFSET)
#define FB_GICD_ICENABLER0	(FB_GICD + FB_GICD_ICENABLER0_OFFSET)
#define FB_GICD_ISPENDR0	(FB_GICD + FB_GICD_ISPENDR0_OFFSET)
#define FB_GICD_ICPENDR0	(FB_GICD + FB_GICD_ICPENDR0_OFFSET)
#define FB_GICD_ISACTIVER0	(FB_GICD + FB_GICD_ISACTIVER0_OFFSET)
#define FB_GICD_ICACTIVER0	(FB_GICD + FB_GICD_ICACTIVER0_OFFSET)
#define FB_GICD_IPRIORITYR0	(FB_GICD + FB_GICD_IPRIORITYR0_OFFSET)
#define FB_GICD_ITARGETSR0	(FB_GICD + FB_GICD_ITARGETSR0_OFFSET)
#define FB_GICD_ICFGR0	(FB_GICD + FB_GICD_ICFGR0_OFFSET)
#define FB_GICD_IGRPMODR0	(FB_GICD + FB_GICD_IGRPMODR0_OFFSET)
#define FB_GICD_NSACR0	(FB_GICD + FB_GICD_NSACR0_OFFSET)
#define FB_GICD_SGIR	(FB_GICD + FB_GICD_SGIR_OFFSET)
#define FB_GICD_CPENDSGIR0	(FB_GICD + FB_GICD_CPENDSGIR0_OFFSET)
#define FB_GICD_SPENDSGIR0	(FB_GICD + FB_GICD_SPENDSGIR0_OFFSET)
#define FB_GICD_INMIR0	(FB_GICD + FB_GICD_INMIR0_OFFSET)
#define FB_GICD_IGROUPR0E	(FB_GICD + FB_GICD_IGROUPR0E_OFFSET)
#define FB_GICD_ISENABLER0E	(FB_GICD + FB_GICD_ISENABLER0E_OFFSET)
#define FB_GICD_ICENABLER0E	(FB_GICD + FB_GICD_ICENABLER0E_OFFSET)
#define FB_GICD_ISPENDR0E	(FB_GICD + FB_GICD_ISPENDR0E_OFFSET)
#define FB_GICD_ICPENDR0E	(FB_GICD + FB_GICD_ICPENDR0E_OFFSET)
#define FB_GICD_ISACTIVER0E	(FB_GICD + FB_GICD_ISACTIVER0E_OFFSET)
#define FB_GICD_ICACTIVER0E	(FB_GICD + FB_GICD_ICACTIVER0E_OFFSET)
#define FB_GICD_IPRIORITYR0E	(FB_GICD + FB_GICD_IPRIORITYR0E_OFFSET)
#define FB_GICD_ICFGR0E	(FB_GICD + FB_GICD_ICFGR0E_OFFSET)
#define FB_GICD_IGRPMODR0E	(FB_GICD + FB_GICD_IGRPMODR0E_OFFSET)
#define FB_GICD_NSACR0E	(FB_GICD + FB_GICD_NSACR0E_OFFSET)
#define FB_GICD_INMIR0E	(FB_GICD + FB_GICD_INMIR0E_OFFSET)
#define FB_GICD_IROUTER0	(FB_GICD + FB_GICD_IROUTER0_OFFSET)
#define FB_GICD_IROUTER0E	(FB_GICD + FB_GICD_IROUTER0E_OFFSET)
#define FB_GICD_PIDR2	(FB_GICD + FB_GICD_PIDR2_OFFSET)

/* register bits */
#define FB_GICD_CTLR_EnableGrp1 (1u << 0)
#define FB_GICD_CTLR_EnableGrp1A (1u << 1)
#define FB_GICD_CTLR_ARE_NS (1u << 4)
#define FB_GICD_CTLR_RWP (1ul << 31)

/******************************************************************************
 *********************** GICR: REDISTRIBUTOR REGISTERS ************************
 ******************************************************************************/

/* NOTE: pretty much required */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GICR_BASE
#define FB_GICR GICR_BASE
#endif

#ifdef GICR_SIZE
#define FB_GICR_SIZE GICR_SIZE
#endif

#if defined(FB_GICR) && defined(FB_GICR_SIZE)
#define GICR_ENABLED

/* frame size is 64KB for GICv3 and 128KB for GICv4                        *
 * NOTE: do we want to take this as min(frames, cpus) or just frames? */
#define FB_GICR_FRAME_SIZE ((GIC_VERSION) == 3 ? 0x20000 : 0x40000)
#define MAX_FB_GICR ((FB_GICR_SIZE) / (FB_GICR_FRAME_SIZE))
#else
#if !defined(HOST_TESTING_MODE) || HOST_TESTING_MODE == 0
#pragma message ( "Undefined GICR stuff. Check project/sanctuary/BUILD.gn." )
#endif
#endif

/* register offsets */ 
#define FB_GICR_CTLR_OFFSET	0x0000
#define FB_GICR_IIDR_OFFSET	0x0004
#define FB_GICR_TYPER_OFFSET	0x0008
#define FB_GICR_STATUSR_OFFSET	0x0010
#define FB_GICR_WAKER_OFFSET	0x0014
#define FB_GICR_MPAMIDR_OFFSET	0x0018
#define FB_GICR_PARTIDR_OFFSET	0x001C
#define FB_GICR_SETLPIR_OFFSET	0x0040
#define FB_GICR_CLRLPIR_OFFSET	0x0048
#define FB_GICR_PROPBASER_OFFSET	0x0070
#define FB_GICR_PENDBASER_OFFSET	0x0078
#define FB_GICR_INVLPIR_OFFSET	0x00A0
#define FB_GICR_INVALLR_OFFSET	0x00B0
#define FB_GICR_SYNCR_OFFSET	0x00C0
#define FB_GICR_IGROUPR0_OFFSET	0x0080
#define FB_GICR_IGROUPR1E_OFFSET	0x0084
#define FB_GICR_IGROUPR2E_OFFSET	0x0088
#define FB_GICR_ISENABLER0_OFFSET	0x0100
#define FB_GICR_ISENABLER1E_OFFSET	0x0104
#define FB_GICR_ISENABLER2E_OFFSET	0x0108
#define FB_GICR_ICENABLER0_OFFSET	0x0180
#define FB_GICR_ICENABLER1E_OFFSET	0x0184
#define FB_GICR_ICENABLER2E_OFFSET	0x0188
#define FB_GICR_ISPENDR0_OFFSET	0x0200
#define FB_GICR_ISPENDR1E_OFFSET	0x0204
#define FB_GICR_ISPENDR2E_OFFSET	0x0208
#define FB_GICR_ICPENDR0_OFFSET	0x0280
#define FB_GICR_ICPENDR1E_OFFSET	0x0284
#define FB_GICR_ICPENDR2E_OFFSET	0x0288
#define FB_GICR_ISACTIVER0_OFFSET	0x0300
#define FB_GICR_ISACTIVER1E_OFFSET	0x0304
#define FB_GICR_ISACTIVER2E_OFFSET	0x0308
#define FB_GICR_ICACTIVER0_OFFSET	0x0380
#define FB_GICR_ICACTIVER1E_OFFSET	0x0384
#define FB_GICR_ICACTIVER2E_OFFSET	0x0388
#define FB_GICR_IPRIORITYR0_OFFSET	0x0400
#define FB_GICR_IPRIORITYR1_OFFSET	0x0404
#define FB_GICR_IPRIORITYR2_OFFSET	0x0408
#define FB_GICR_IPRIORITYR3_OFFSET	0x040C
#define FB_GICR_IPRIORITYR4_OFFSET	0x0410
#define FB_GICR_IPRIORITYR5_OFFSET	0x0414
#define FB_GICR_IPRIORITYR6_OFFSET	0x0418
#define FB_GICR_IPRIORITYR7_OFFSET	0x041C
#define FB_GICR_IPRIORITYR8E_OFFSET	0x0420
#define FB_GICR_IPRIORITYR9E_OFFSET	0x0424
#define FB_GICR_IPRIORITYR10E_OFFSET	0x0428
#define FB_GICR_IPRIORITYR11E_OFFSET	0x042C
#define FB_GICR_IPRIORITYR12E_OFFSET	0x0430
#define FB_GICR_IPRIORITYR13E_OFFSET	0x0434
#define FB_GICR_IPRIORITYR14E_OFFSET	0x0438
#define FB_GICR_IPRIORITYR15E_OFFSET	0x043C
#define FB_GICR_IPRIORITYR16E_OFFSET	0x0440
#define FB_GICR_IPRIORITYR17E_OFFSET	0x0444
#define FB_GICR_IPRIORITYR18E_OFFSET	0x0448
#define FB_GICR_IPRIORITYR19E_OFFSET	0x044C
#define FB_GICR_IPRIORITYR20E_OFFSET	0x0450
#define FB_GICR_IPRIORITYR21E_OFFSET	0x0454
#define FB_GICR_IPRIORITYR22E_OFFSET	0x0458
#define FB_GICR_IPRIORITYR23E_OFFSET	0x045C
#define FB_GICR_ICFGR0_OFFSET	0x0C00
#define FB_GICR_ICFGR1E_OFFSET	0x0C04
#define FB_GICR_ICFGR2E_OFFSET	0x0C08
#define FB_GICR_ICFGR3E_OFFSET	0x0C0C
#define FB_GICR_ICFGR4E_OFFSET	0x0C10
#define FB_GICR_ICFGR5E_OFFSET	0x0C14
#define FB_GICR_ICFGR1_OFFSET	0x0C18
#define FB_GICR_IGRPMODR0_OFFSET	0x0D00
#define FB_GICR_IGRPMODR1E_OFFSET	0x0D04
#define FB_GICR_IGRPMODR2E_OFFSET	0x0D08
#define FB_GICR_NSACR_OFFSET	0x0E00
#define FB_GICR_INMIR0_OFFSET	0x0F80
#define FB_GICR_INMIR1E_OFFSET	0x0F84
#define FB_GICR_INMIR2E_OFFSET	0x0F88
#define FB_GICR_PIDR2_OFFSET	0xffe8

/* register addresses */
#define FB_GICR_CTLR	(FB_GICR + FB_GICR_CTLR_OFFSET)
#define FB_GICR_IIDR	(FB_GICR + FB_GICR_IIDR_OFFSET)
#define FB_GICR_TYPER	(FB_GICR + FB_GICR_TYPER_OFFSET)
#define FB_GICR_STATUSR	(FB_GICR + FB_GICR_STATUSR_OFFSET)
#define FB_GICR_STATUSR	(FB_GICR + FB_GICR_STATUSR_OFFSET)
#define FB_GICR_WAKER	(FB_GICR + FB_GICR_WAKER_OFFSET)
#define FB_GICR_MPAMIDR	(FB_GICR + FB_GICR_MPAMIDR_OFFSET)
#define FB_GICR_PARTIDR	(FB_GICR + FB_GICR_PARTIDR_OFFSET)
#define FB_GICR_SETLPIR	(FB_GICR + FB_GICR_SETLPIR_OFFSET)
#define FB_GICR_CLRLPIR	(FB_GICR + FB_GICR_CLRLPIR_OFFSET)
#define FB_GICR_PROPBASER	(FB_GICR + FB_GICR_PROPBASER_OFFSET)
#define FB_GICR_PENDBASER	(FB_GICR + FB_GICR_PENDBASER_OFFSET)
#define FB_GICR_INVLPIR	(FB_GICR + FB_GICR_INVLPIR_OFFSET)
#define FB_GICR_INVALLR	(FB_GICR + FB_GICR_INVALLR_OFFSET)
#define FB_GICR_SYNCR	(FB_GICR + FB_GICR_SYNCR_OFFSET)
#define FB_GICR_IGROUPR0	(FB_GICR + FB_GICR_IGROUPR0_OFFSET)
#define FB_GICR_IGROUPR1E	(FB_GICR + FB_GICR_IGROUPR1E_OFFSET)
#define FB_GICR_IGROUPR2E	(FB_GICR + FB_GICR_IGROUPR2E_OFFSET)
#define FB_GICR_ISENABLER0	(FB_GICR + FB_GICR_ISENABLER0_OFFSET)
#define FB_GICR_ISENABLER1E	(FB_GICR + FB_GICR_ISENABLER1E_OFFSET)
#define FB_GICR_ISENABLER2E	(FB_GICR + FB_GICR_ISENABLER2E_OFFSET)
#define FB_GICR_ICENABLER0	(FB_GICR + FB_GICR_ICENABLER0_OFFSET)
#define FB_GICR_ICENABLER1E	(FB_GICR + FB_GICR_ICENABLER1E_OFFSET)
#define FB_GICR_ICENABLER2E	(FB_GICR + FB_GICR_ICENABLER2E_OFFSET)
#define FB_GICR_ISPENDR0	(FB_GICR + FB_GICR_ISPENDR0_OFFSET)
#define FB_GICR_ISPENDR1E	(FB_GICR + FB_GICR_ISPENDR1E_OFFSET)
#define FB_GICR_ISPENDR2E	(FB_GICR + FB_GICR_ISPENDR2E_OFFSET)
#define FB_GICR_ICPENDR0	(FB_GICR + FB_GICR_ICPENDR0_OFFSET)
#define FB_GICR_ICPENDR1E	(FB_GICR + FB_GICR_ICPENDR1E_OFFSET)
#define FB_GICR_ICPENDR2E	(FB_GICR + FB_GICR_ICPENDR2E_OFFSET)
#define FB_GICR_ISACTIVER0	(FB_GICR + FB_GICR_ISACTIVER0_OFFSET)
#define FB_GICR_ISACTIVER1E	(FB_GICR + FB_GICR_ISACTIVER1E_OFFSET)
#define FB_GICR_ISACTIVER2E	(FB_GICR + FB_GICR_ISACTIVER2E_OFFSET)
#define FB_GICR_ICACTIVER0	(FB_GICR + FB_GICR_ICACTIVER0_OFFSET)
#define FB_GICR_ICACTIVER1E	(FB_GICR + FB_GICR_ICACTIVER1E_OFFSET)
#define FB_GICR_ICACTIVER2E	(FB_GICR + FB_GICR_ICACTIVER2E_OFFSET)
#define FB_GICR_IPRIORITYR0	(FB_GICR + FB_GICR_IPRIORITYR0_OFFSET)
#define FB_GICR_IPRIORITYR1	(FB_GICR + FB_GICR_IPRIORITYR1_OFFSET)
#define FB_GICR_IPRIORITYR2	(FB_GICR + FB_GICR_IPRIORITYR2_OFFSET)
#define FB_GICR_IPRIORITYR3	(FB_GICR + FB_GICR_IPRIORITYR3_OFFSET)
#define FB_GICR_IPRIORITYR4	(FB_GICR + FB_GICR_IPRIORITYR4_OFFSET)
#define FB_GICR_IPRIORITYR5	(FB_GICR + FB_GICR_IPRIORITYR5_OFFSET)
#define FB_GICR_IPRIORITYR6	(FB_GICR + FB_GICR_IPRIORITYR6_OFFSET)
#define FB_GICR_IPRIORITYR7	(FB_GICR + FB_GICR_IPRIORITYR7_OFFSET)
#define FB_GICR_IPRIORITYR8E	(FB_GICR + FB_GICR_IPRIORITYR8E_OFFSET)
#define FB_GICR_IPRIORITYR9E	(FB_GICR + FB_GICR_IPRIORITYR9E_OFFSET)
#define FB_GICR_IPRIORITYR10E	(FB_GICR + FB_GICR_IPRIORITYR10E_OFFSET)
#define FB_GICR_IPRIORITYR11E	(FB_GICR + FB_GICR_IPRIORITYR11E_OFFSET)
#define FB_GICR_IPRIORITYR12E	(FB_GICR + FB_GICR_IPRIORITYR12E_OFFSET)
#define FB_GICR_IPRIORITYR13E	(FB_GICR + FB_GICR_IPRIORITYR13E_OFFSET)
#define FB_GICR_IPRIORITYR14E	(FB_GICR + FB_GICR_IPRIORITYR14E_OFFSET)
#define FB_GICR_IPRIORITYR15E	(FB_GICR + FB_GICR_IPRIORITYR15E_OFFSET)
#define FB_GICR_IPRIORITYR16E	(FB_GICR + FB_GICR_IPRIORITYR16E_OFFSET)
#define FB_GICR_IPRIORITYR17E	(FB_GICR + FB_GICR_IPRIORITYR17E_OFFSET)
#define FB_GICR_IPRIORITYR18E	(FB_GICR + FB_GICR_IPRIORITYR18E_OFFSET)
#define FB_GICR_IPRIORITYR19E	(FB_GICR + FB_GICR_IPRIORITYR19E_OFFSET)
#define FB_GICR_IPRIORITYR20E	(FB_GICR + FB_GICR_IPRIORITYR20E_OFFSET)
#define FB_GICR_IPRIORITYR21E	(FB_GICR + FB_GICR_IPRIORITYR21E_OFFSET)
#define FB_GICR_IPRIORITYR22E	(FB_GICR + FB_GICR_IPRIORITYR22E_OFFSET)
#define FB_GICR_IPRIORITYR23E	(FB_GICR + FB_GICR_IPRIORITYR23E_OFFSET)
#define FB_GICR_ICFGR0	(FB_GICR + FB_GICR_ICFGR0_OFFSET)
#define FB_GICR_ICFGR1E	(FB_GICR + FB_GICR_ICFGR1E_OFFSET)
#define FB_GICR_ICFGR2E	(FB_GICR + FB_GICR_ICFGR2E_OFFSET)
#define FB_GICR_ICFGR1	(FB_GICR + FB_GICR_ICFGR1_OFFSET)
#define FB_GICR_IGRPMODR0	(FB_GICR + FB_GICR_IGRPMODR0_OFFSET)
#define FB_GICR_IGRPMODR1E	(FB_GICR + FB_GICR_IGRPMODR1E_OFFSET)
#define FB_GICR_IGRPMODR2E	(FB_GICR + FB_GICR_IGRPMODR2E_OFFSET)
#define FB_GICR_NSACR	(FB_GICR + FB_GICR_NSACR_OFFSET)
#define FB_GICR_INMIR0	(FB_GICR + FB_GICR_INMIR0_OFFSET)
#define FB_GICR_INMIR1E	(FB_GICR + FB_GICR_INMIR1E_OFFSET)
#define FB_GICR_INMIR2E	(FB_GICR + FB_GICR_INMIR2E_OFFSET)
#define FB_GICR_PIDR2	(FB_GICR + FB_GICR_PIDR2_OFFSET)
#define FB_GICR_VPROPBASER	(FB_GICR + FB_GICR_VPROPBASER_OFFSET)
#define FB_GICR_VPENDBASER	(FB_GICR + FB_GICR_VPENDBASER_OFFSET)
#define FB_GICR_VSGIR	(FB_GICR + FB_GICR_VSGIR_OFFSET)
#define FB_GICR_VSGIPENDR	(FB_GICR + FB_GICR_VSGIPENDR_OFFSET)

/******************************************************************************
 *************** GITS: INTERRUPT TRANSLATION SERVICE REGISTERS ****************
 ******************************************************************************/

/* NOTE: requires a GICv3 but not a physical ITS controller         *
 *       each guest can have multiple ITS controllers               *
 *       each ITS controller must have non-overlapping MIMO regions *
 *       there can be multiple implementations of ITS               */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GITS_BASE
#define FB_GITS GITS_BASE
#endif

#ifdef GITS_SIZE
#define FB_GITS_SIZE GITS_SIZE
#endif

#if defined(FB_GITS) && defined(FB_GITS_SIZE)
#define GITS_ENABLED
#endif

/* register offsets */
#define FB_GITS_CTLR_OFFSET	0x0000
#define FB_GITS_IIDR_OFFSET	0x0004
#define FB_GITS_TYPER_OFFSET 	0x0008
#define FB_GITS_MPAMIDR_OFFSET	0x0010
#define FB_GITS_PARTIDR_OFFSET	0x0014
#define FB_GITS_MPIDR_OFFSET	0x0018
#define FB_GITS_STATUSR_OFFSET	0x0040
#define FB_GITS_UMSIR_OFFSET 	0x0048
#define FB_GITS_CBASER_OFFSET	0x0080
#define FB_GITS_CWRITER_OFFSET	0x0088
#define FB_GITS_CREADR_OFFSET	0x0090
#define FB_GITS_BASER0_OFFSET	0x0100
#define FB_GITS_BASER1_OFFSET	0x0108
#define FB_GITS_BASER2_OFFSET	0x0110
#define FB_GITS_BASER3_OFFSET	0x0118
#define FB_GITS_BASER4_OFFSET	0x0120
#define FB_GITS_BASER5_OFFSET	0x0128
#define FB_GITS_BASER6_OFFSET	0x0130
#define FB_GITS_BASER7_OFFSET	0x0138
#define FB_GITS_SGIR_OFFSET	0x20020

/* register addresses */
#define FB_GITS_CTLR		(FB_GITS +  FB_GITS_CTLR_OFFSET)
#define FB_GITS_IIDR		(FB_GITS +  FB_GITS_IIDR_OFFSET)
#define FB_GITS_TYPER		(FB_GITS +  FB_GITS_TYPER_OFFSET)
#define FB_GITS_MPAMIDR	(FB_GITS +  FB_GITS_MPAMIDR_OFFSET)
#define FB_GITS_PARTIDR	(FB_GITS +  FB_GITS_PARTIDR_OFFSET)
#define FB_GITS_MPIDR		(FB_GITS +  FB_GITS_MPIDR_OFFSET)
#define FB_GITS_STATUSR	(FB_GITS +  FB_GITS_STATUSR_OFFSET)
#define FB_GITS_UMSIR		(FB_GITS +  FB_GITS_UMSIR_OFFSET)
#define FB_GITS_CBASER		(FB_GITS +  FB_GITS_CBASER_OFFSET)
#define FB_GITS_CWRITER	(FB_GITS +  FB_GITS_CWRITER_OFFSET)
#define FB_GITS_CREADR		(FB_GITS +  FB_GITS_CREADR_OFFSET)
#define FB_GITS_BASER0		(FB_GITS +  FB_GITS_BASER0_OFFSET)
#define FB_GITS_BASER1		(FB_GITS +  FB_GITS_BASER1_OFFSET)
#define FB_GITS_BASER2		(FB_GITS +  FB_GITS_BASER2_OFFSET)
#define FB_GITS_BASER3		(FB_GITS +  FB_GITS_BASER3_OFFSET)
#define FB_GITS_BASER4		(FB_GITS +  FB_GITS_BASER4_OFFSET)
#define FB_GITS_BASER5		(FB_GITS +  FB_GITS_BASER5_OFFSET)
#define FB_GITS_BASER6		(FB_GITS +  FB_GITS_BASER6_OFFSET)
#define FB_GITS_BASER7		(FB_GITS +  FB_GITS_BASER7_OFFSET)
#define FB_GITS_SGIR		(FB_GITS +  FB_GITS_SGIR_OFFSET)

/******************************************************************************
 ******************* GICC: PHYSICAL CPU INTERFACE REGISTERS *******************
 ******************************************************************************/

/* NOTE: legacy from v2, not really used on v3 (except FVP) */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GICC_BASE
#define FB_GICC GICC_BASE
#endif

#ifdef GICC_SIZE
#define FB_GICC_SIZE GICC_SIZE
#endif

#if defined(FB_GICC) && defined(FB_GICC_SIZE)
#define GICC_ENABLED
#endif

/******************************************************************************
 ******************* GICV: VIRTUAL CPU INTERFACE REGISTERS ********************
 ******************************************************************************/

/* NOTE: legacy from v2, not really used on v3 (except FVP) */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GICV_BASE
#define FB_GICV GICV_BASE
#endif

#ifdef GICV_SIZE
#define FB_GICV_SIZE GICV_SIZE
#endif

#if defined(FB_GICV) && defined(FB_GICV_SIZE)
#define GICV_ENABLED
#endif

/******************************************************************************
 ***************** GICH: VIRTUAL INTERFACE CONTROL REGISTERS ******************
 ******************************************************************************/

/* NOTE: legacy from v2, not really used on v3 (except FVP) */

/* redefine exports from `build/toolchain/embedded.gni` *
 * these exports depend on `project/sanctuary/BUILD.gn` */
#ifdef GICH_BASE
#define FB_GICH GICH_BASE
#endif

#ifdef GICH_SIZE
#define FB_GICH_SIZE GICH_SIZE
#endif

#if defined(FB_GICH) && defined(FB_GICH_SIZE)
#define GICH_ENABLED
#endif

/******************************************************************************
 ************************** PUBLIC STRUCTURES & API ***************************
 ******************************************************************************/
/**
 * NOTE: currently to require only a single mapped region, we need to map
 * the correct offsets between the GIC components as well. 
 * This could be changed in the future to redcue memory usage.
 * (Tradeoff between boot time and memory usage)
 */
#ifdef GITS_ENABLED
struct virt_gic {
	uint32_t gicd[FB_GICD_SIZE/sizeof(uint32_t)];
	uint32_t offset_dummy1[(FB_GITS-FB_GICD-FB_GICD_SIZE)/sizeof(uint32_t)];
	uint32_t gits[FB_GITS_SIZE/sizeof(uint32_t)];
	uint32_t offset_dummy2[(FB_GICR-FB_GITS-FB_GITS_SIZE)/sizeof(uint32_t)];
	uint32_t gicr[MAX_FB_GICR][FB_GICR_SIZE/MAX_FB_GICR/sizeof(uint32_t)];
	// TODO more GIC subcomponents (GICV, GICH, etc.) to be added
};
#else
#if defined(GICD_ENABLED) && defined(GICR_ENABLED)
struct virt_gic {
	uint32_t gicd[FB_GICD_SIZE/sizeof(uint32_t)];
	uint32_t offset_dummy[(FB_GICR-FB_GICD-FB_GICD_SIZE)/sizeof(uint32_t)];
	uint32_t gicr[MAX_FB_GICR][FB_GICR_SIZE/MAX_FB_GICR/sizeof(uint32_t)];
	// TODO more GIC subcomponents (GICV, GICH, etc.) to be added
};
#endif
#endif

struct interrupt_owner {
	struct vm* vm;
	/*
	 * INT_UNKNOWN -> status of the interrupt is unknown
	 * INT_INACTIVE -> interrupt is not active
	 * INT_ROUTED -> interrupt is activated and is routed to a VM / to a physical CPU of a VM
	 * INT_DANGLING -> interrupt activated by VM but not routed to one of its CPUs
	 */
	uint8_t status;
};

typedef struct {
    union {
        struct {
            uint64_t Aff0 :  8;  /* [7:0]   Affinity 0                      */
            uint64_t Aff1 :  8;  /* [15:8]  Affinity 1                      */
            uint64_t Aff2 :  8;  /* [23:16] Affinity 2                      */
            uint64_t MT   :  1;  /* [24]    Multi-Threaded                  */
            uint64_t Res0 :  5;  /* [29:25] Reserved                        */
            uint64_t U    :  1;  /* [30]    Uniprocessor                    */
            uint64_t MPEA :  1;  /* [31]    Multi-Proc Extensions Available */
            uint64_t Aff3 :  8;  /* [39:32] Affinity 3                      */
            uint64_t Res3 : 24;  /* [63:40] Reserved                        */
        };
        uint64_t raw;   /* direct access to full register */
    };
} mpidr_t;

uint64_t aff_to_no(uint64_t data);
void reroute_all_interrupts(struct vm* vm, uint32_t cpuid);
bool access_gicv3(uintreg_t esr, uintreg_t far, uint8_t pc_inc, struct vcpu *vcpu, struct vcpu_fault_info *info);
bool icc_icv_is_register_access(uintreg_t esr);
bool icc_icv_process_access(struct vcpu *vcpu, uintreg_t esr);
bool is_cache_maintenance(uintreg_t esr);
bool process_cache_maintenance(struct vcpu *vcpu, uintreg_t esr);
void init_gic();
void init_vgic(struct vm* vm);
