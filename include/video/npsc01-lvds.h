/*
 * drivers/video/nusmart/nu7t_dsi.h
 *
 * Copyright (C) 2015 Nufront Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __NPSC01_LVDS_H__
#define __NPSC01_LVDS_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <asm/irq.h>

#include <asm/mach-types.h>
#include <linux/completion.h>
#include <linux/clk.h>

/*LVDS CONTROLLER*/
#define LVDSTX_CTRL  0x80

#define LVDSTX_FORMAT BIT(0)
#define LVDSTX_PIN_ENABLE BIT(3)

#define LVDSTX_PLL_POWER_DOWN BIT(6)
#define LVDSTX_PLL_LOCK BIT(8)


/* D-PHY regs */

#define INNO_DPHY_REG(offset, reg)	(offset << 5 | addr) << 2

#define INNO_DPHY_OFF0_REG01 0x01
#define INNO_DPHY_OFF0_REG03 0x0c
#define INNO_DPHY_OFF0_REG04 0x10
#define INNO_DPHY_OFF0_REG08 0x20
#define INNO_DPHY_OFF0_REG0B 0x2c
#define INNO_DPHY_OFF0_REG10 0x40


#define INTERNAL_LOGIC_ENABLE BIT(7)

#define LVDS_MODE_ENABLE BIT(1)
#define TTL_MODE_ENABLE BIT(2)





struct lvdstx_driver_data
{

	int lvdstx_enabled;
	const char *panel;
	struct fb_videomode *mode;
	struct device_node *display_interface;

	struct mutex lock;
	void __iomem *host_base;

	struct clk *lvdstx_pclk;
//	struct clk *lvdstx_pxlclk;

	struct reset_control *lvdstx_reset;
	struct notifier_block lvdstx_notif;

	struct platform_device *pdev;

	struct work_struct resume_work;
};

//extern unsigned int nufront_prcm_readl(unsigned int reg_offset);
//extern void gen_hdr(unsigned char data_type, unsigned char virtual_chanel, unsigned char wc_lsb, unsigned char wc_msb);
//extern void gen_pld_data(unsigned char b1, unsigned char b2, unsigned b3, unsigned b4);

#endif





