/* linux/arch/arm/mach-nufront/include/mach/timex.h
 *
 * Copyright (c) 2014 Nufront.
 *
 * Nufront - time parameters
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H __FILE__

/* CLOCK_TICK_RATE needs to be evaluatable by the cpp, so making it
 * a variable is useless. It seems as long as we make our timers an
 * exact multiple of HZ, any value that makes a 1->1 correspondence
 * for the time conversion functions to/from jiffies is acceptable.
*/

#define CLOCK_TICK_RATE (50000000 / 16)

#endif /* __ASM_ARCH_TIMEX_H */
