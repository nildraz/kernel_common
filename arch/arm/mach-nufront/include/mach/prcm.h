
#ifndef __NUFRONT_PRCM_H__
#define __NUFRONT_PRCM_H__


#ifdef NUFRONT_PRCM_DIRECT_ACCESS
#define PRCM_BASE                   get_prcm_base()
/* get virtual address of prcm, it call by PRCM_BASE automatic */
void __iomem *get_prcm_base(void);
#else

#define PRCM_BASE 0

/**
 * nufront_prcm_write - Write to a register in PRCM
 *
 * @val:	Value to write to the register
 * @offset:	Register offset in PRCM block
 *
 * Return:	a negative value on error, 0 on success
 */
int nufront_prcm_write(u32 val, u32 offset);

/**
 * nufront_prcm_read - Read a register in PRCM
 *
 * @val:	Pointer to value to be read from PRCM
 * @offset:	Register offset in PRCM block
 *
 * Return:	a negative value on error, 0 on success
 */
int nufront_prcm_read(u32 *val, u32 offset);

#endif



#ifdef CONFIG_ARCH_NPSC01
#include <mach/prcm-npsc01.h>
#endif

#endif
