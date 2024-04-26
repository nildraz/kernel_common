/* include/linux/tl7689_smd.h
 *
 * Copyright (C) 2013 Nufront, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _M3_SMD_H
#define _M3_SMD_H

struct m3_smd_driver_info
{
	void __iomem *m3_s_smd_base;
	void __iomem *m3_p_smd_base;
	struct cdev cdev;
	struct class *m3_smd_class;
	struct device *m3_smd;	//m3_smd
	struct device *dev;	//platform dev
	unsigned int m3s_2ap_irq0;
	unsigned int m3s_2ap_irq1;
	unsigned int m3p_2ap_irq0;
	unsigned int m3p_2ap_irq1;
	unsigned int m3p_wdt_irq;
	unsigned int m3s_wdt_irq;
	unsigned int curr_m3;
	atomic_t is_open;
};

struct m3_shm_head
{
	unsigned char tag[4];
	unsigned int length;
	unsigned int cksum;
};

#define RELEASE 1
#define RESET 0
#define PRINTER 1
#define SCANNER 0
#define M3_INT_SHM_TEST	0x1
#define M3IOC_SETM3P_LOADADDR 	0x101
#define M3IOC_SETM3S_LOADADDR	0x102
#define M3IOC_SETM3_A9_RELEASE_M3P 0x103
#define M3IOC_SETM3_A9_RELEASE_M3S 0x104
#define M3IOC_SETM3_A9_RESET_M3P   0x105
#define M3IOC_SETM3_A9_RESET_M3S   0x106
#define M3IOC_RESET_AVISION_SUBSYS 0x107


#endif
