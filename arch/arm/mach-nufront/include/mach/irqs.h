/*
 * arch/arm/mach-nufront/include/mach/irqs-npsc.h
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H
/*
 * Irqs
 */

#define NR_IRQS_NPSC 256
/*
 * Only define NR_IRQS if less than NR_IRQS_NPSC
 */

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_NPSC)
#undef NR_IRQS
#define NR_IRQS NR_IRQS_NPSC
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_NPSC)
#undef MAX_GIC_NR
#define MAX_GIC_NR NR_IRQS_NPSC
#endif
#endif/* __MACH_IRQS_NPSC_H */
