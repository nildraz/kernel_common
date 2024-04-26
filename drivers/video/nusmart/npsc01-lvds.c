/*
 * drivers/video/nusmart/npsc01_lvds.c
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

#include <asm/io.h>
#include <linux/of.h>
#include <linux/fb.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/ioport.h>
#include <linux/reset.h>
#include <linux/kallsyms.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm-generic/uaccess.h>
#include <mach/pinctrl-nufront.h>
#include <video/npsc01-lvds.h>

#include "panel/npsc01_panel.h"

#define DRIVER_NAME	"npsc01-lvdstx"

#define DBG_ERR		0x8
#define DBG_WARN	0x4
#define DBG_INFO	0x2
#define DBG_DEBUG	0x1

/*
 * Modify /sys/module/npsc01_lvdstx/parameters/dbg_level
 * to regulate print level of this driver
 */
static unsigned int dbg_level = DBG_ERR | DBG_WARN | DBG_INFO;
module_param_named(dbg_level, dbg_level, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define lvds_err(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_ERR)					\
		printk(KERN_ERR "lvds err: " fmt, ##args);		\
	} while(0)

#define lvds_warn(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_ERR)					\
		printk(KERN_WARNING "lvds warn: " fmt, ##args);		\
	} while(0)

#define lvds_info(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_INFO)				\
		printk(KERN_INFO "lvds info: " fmt, ##args);		\
	} while(0)

#define lvds_dbg(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_DEBUG)				\
		printk(KERN_DEBUG "lvds dbg: " fmt, ##args);		\
	} while(0)

#define set_reg_bits(reg_addr, bits)					\
	do {								\
		unsigned int val = readl(reg_addr);			\
		val |= bits;						\
		writel(val, reg_addr);					\
	} while (0)

#define clr_reg_bits(reg_addr, bits)					\
	do {								\
		unsigned int val = readl(reg_addr);			\
		val &= ~bits;						\
		writel(val, reg_addr);					\
	} while (0)

#define	LVDS_CUR_POSITION lvds_dbg("func:%s line:%d\n", __func__, __LINE__)
#define	CHECK_FEEDBACK_DIVIDER(fbdiv) (fbdiv >= 12 && fbdiv <= 511 && fbdiv != 15)? 0:-1

//static void __iomem *lvdstx_host_base;
//static int (*panel_codes_init) (unsigned int);
static int npsc01_lvdstx_clock_enable(struct lvdstx_driver_data *drv_data);
static int npsc01_lvdstx_clock_disable(struct lvdstx_driver_data *drv_data);



static int npsc01_lvdstx_ctrl_enable(struct lvdstx_driver_data *drv_data)
{
	void __iomem *host_base = drv_data->host_base;
	u32 wdata, rdata;
	rdata = readl(host_base + 0x80);
	wdata = rdata | (0x1 << 3);
	writel(wdata, host_base + 0x80);

	wdata = 0x2;
	writel(wdata, host_base + 0x0c);

	wdata = 0x40;
	writel(wdata, host_base + 0x38);


	wdata = 0xf8;
	writel(wdata, host_base + 0x2c);

	wdata = 0xaa;
	writel(wdata, host_base + 0x10);

	wdata = 0x01;
	writel(wdata, host_base + 0x40);


	wdata = 0x12;
	writel(wdata, host_base + 0x04);

	rdata = readl(host_base + 0x80);
	
	msleep(100);
   
	rdata = readl(host_base + 0x80);
	if((rdata & (0x1 << 8)) == 0x0)
	{
		lvds_err("lvds pll locked failed!.\n");
		return -1;
	}

	wdata = 0x92;
	writel(wdata, host_base + 0x04);

	return 0;
}

static int npsc01_lvdstx_ctrl_disable(struct lvdstx_driver_data *drv_data)
{
	void __iomem *host_base = drv_data->host_base;
	u32 wdata, rdata;
	wdata = 0x12;
	writel(wdata, host_base + 0x04);

	wdata = 0x04;
	writel(wdata, host_base + 0x2c);

	rdata = readl(host_base + 0x80);
	wdata = rdata & ~(0x1 << 3);
	writel(wdata, host_base + 0x80);

	return 0;
}


static int npsc01_lvdstx_idle_in(struct lvdstx_driver_data *drv_data)
{
	if(!drv_data->lvdstx_enabled) {
		lvds_warn("lvdstx_enabled:%d into idle is ignored.\n", drv_data->lvdstx_enabled);
		return 0;
	}
	else
		drv_data->lvdstx_enabled = 0;

	npsc01_lvdstx_ctrl_disable(drv_data);
	npsc01_lvdstx_clock_disable(drv_data);

	return 0;
}


static int npsc01_lvdstx_idle_out(struct lvdstx_driver_data *drv_data)
{
	if(drv_data->lvdstx_enabled) {
		lvds_warn("lvdstx_enabled:%d outof idle is ignored.\n", drv_data->lvdstx_enabled);
		return 0;
	}

	npsc01_lvdstx_clock_enable(drv_data);
	npsc01_lvdstx_ctrl_enable(drv_data);

	drv_data->lvdstx_enabled = 1;
	return 0;
}

static int npsc01_lvdstx_suspend(struct lvdstx_driver_data *drv_data)
{
	if(!drv_data->lvdstx_enabled) {
		lvds_warn("lvdstx_enabled:%d suspend is ignored.\n", drv_data->lvdstx_enabled);
		return 0;
	}
	else
		drv_data->lvdstx_enabled = 0;

	npsc01_lvdstx_ctrl_disable(drv_data);
	npsc01_lvdstx_clock_disable(drv_data);
//	reset_control_assert(drv_data->lvdstx_reset);

	return 0;
}

static int npsc01_lvdstx_resume(struct lvdstx_driver_data *drv_data)
{
	if(drv_data->lvdstx_enabled) {
		lvds_warn("lvdstx_enabled:%d resume is ignored.\n", drv_data->lvdstx_enabled);
		return 0;
	}

//	reset_control_deassert(drv_data->lvdstx_reset);
	npsc01_lvdstx_clock_enable(drv_data);
	npsc01_lvdstx_ctrl_enable(drv_data);

	drv_data->lvdstx_enabled = 1;
	return 0;
}

int npsc01_lvdsx_plat_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct lvdstx_driver_data *data = platform_get_drvdata(pdev);
	return npsc01_lvdstx_suspend(data);
}

static void npsc01_lvdstx_resume_work(struct work_struct *work)
{
	struct lvdstx_driver_data *data = container_of(work, struct lvdstx_driver_data, resume_work);
	npsc01_lvdstx_resume(data);
}

int npsc01_lvdstx_plat_resume(struct platform_device *pdev)
{
	struct lvdstx_driver_data *data = platform_get_drvdata(pdev);
//	if (num_registered_fb > 0 && registered_fb[0])
//		device_pm_wait_for_dev(&pdev->dev, registered_fb[0]->dev);

	schedule_work(&data->resume_work);

	return 0;
}

static int npsc01_lvdstx_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct lvdstx_driver_data *drv_data = container_of(nb, struct lvdstx_driver_data, lvdstx_notif);
//	int ret = 0;

	switch(event) {

	case FB_EVENT_SUSPEND:
		npsc01_lvdstx_suspend(drv_data);
		break;
		
	case FB_EVENT_RESUME:
		npsc01_lvdstx_resume(drv_data);
		break;

	case FB_EVENT_IDLE_IN:
		npsc01_lvdstx_idle_in(drv_data);
		break;
	   
	case FB_EVENT_IDLE_OUT:
		npsc01_lvdstx_idle_out(drv_data);
		break;
	}

	return 0;
}

static int npsc01_lvdstx_notifier_register(struct lvdstx_driver_data *drv_data)
{
	memset(&drv_data->lvdstx_notif, 0, sizeof(drv_data->lvdstx_notif));
	drv_data->lvdstx_notif.notifier_call = npsc01_lvdstx_notifier_callback;
	drv_data->lvdstx_notif.priority = 2;

	return fb_register_client(&drv_data->lvdstx_notif);
}

static int npsc01_lvdstx_get_resource(struct platform_device *pdev)
{
	struct resource *res_mem = NULL;
//	struct resource *res_irq = NULL;
	struct lvdstx_driver_data *drv_data = platform_get_drvdata(pdev);

	lvds_info("pdev->name = %s\n", pdev->name);
	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		lvds_err("Failed to get mem resource.\n");
		return -ENXIO;
	}
	lvds_info("res_mem->start = 0x%08x\n", res_mem->start);

	res_mem = request_mem_region(res_mem->start, resource_size(res_mem), DRIVER_NAME);
	if (!res_mem) {
		lvds_err("Failed to request mem region.\n");
		return -EBUSY;
	}

	drv_data->host_base = ioremap_nocache(res_mem->start, res_mem->end - res_mem->start + 1);
	lvds_info("host_base = 0x%08x\n",(unsigned int)drv_data->host_base);
/*
	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq) {
		lvds_err("Failed to get irq resource.\n");
		return -ENXIO;
	}

	drv_data->lvdstx_irq0 = res_irq->start;
	drv_data->lvdstx_irq1 = res_irq->start - 1;
*/
	return 0;
}

static int npsc01_lvdstx_clock_enable(struct lvdstx_driver_data *drv_data)
{
		clk_prepare_enable(drv_data->lvdstx_pclk);
//		clk_prepare_enable(drv_data->lvdstx_pxlclk);
		return 0;

}

static int npsc01_lvdstx_clock_disable(struct lvdstx_driver_data *drv_data)
{
		clk_disable_unprepare(drv_data->lvdstx_pclk);
//		clk_disable_unprepare(drv_data->lvdstx_pxlclk);
		return 0;
}

static int npsc01_lvdstx_clock_init(struct platform_device *pdev)
{
	const char *name;
	struct lvdstx_driver_data *drv_data = platform_get_drvdata(pdev);

//	drv_data->lvdstx_reset = devm_reset_control_get(&pdev->dev, "lvdstx_reset");

	of_property_read_string_index(pdev->dev.of_node, "clock-names", 0, &name);
	drv_data->lvdstx_pclk = devm_clk_get(&pdev->dev, name);
/*
	of_property_read_string_index(pdev->dev.of_node, "clock-names", 1, &name);
	drv_data->lvdstx_pxlclk = devm_clk_get(&pdev->dev, name);
*/
	if (IS_ERR(drv_data->lvdstx_pclk)) {
		lvds_err("LVDSTX clocks are not all avaiable.\n");
	}

/*
	if (IS_ERR(drv_data->lvdstx_reset) || IS_ERR(drv_data->lvdstx_refclk)
			|| IS_ERR(drv_data->lvdstx_cfgclk) || IS_ERR(drv_data->lvdstx_pclk)
			|| IS_ERR(drv_data->lvdstx_dpipclk)) {
		lvds_err("LVDSTX clocks are not all avaiable.\n");
		return  -EINVAL;
	}
*/
	return 0;
}

static int npsc01_lvdstx_parse_display_interface(struct platform_device *pdev)
{
	struct lvdstx_driver_data *drv_data = platform_get_drvdata(pdev);

	drv_data->display_interface = of_parse_phandle(pdev->dev.of_node, "display_interface", 0);
	if (!drv_data->display_interface) {
		lvds_err("Failed to get display_interface for %s.\n",
				pdev->dev.of_node->full_name);
		return -ENODEV;
	}

	if((strcmp(drv_data->display_interface->name, "LVDS") && strcmp(drv_data->display_interface->name, "lvds"))) {
		lvds_warn("%s display interface doesn't need LVDSTX driver.\n",
				drv_data->display_interface->name);
		return -ENODEV;
	}

	return 0;
}

static int npsc01_lvdstx_probe(struct platform_device *pdev)
{
	u32 ret = 0;
	struct lvdstx_driver_data *drv_data = NULL;

	drv_data = kzalloc(sizeof(struct lvdstx_driver_data), GFP_KERNEL);
	if(!drv_data) {
		lvds_err("Failed to alloc lvdstx driver data.\n");
		return -ENXIO;
	}

	drv_data->pdev = pdev;
	platform_set_drvdata(pdev, drv_data);

	ret = npsc01_lvdstx_parse_display_interface(pdev);
	if(ret) {
		dev_err(&pdev->dev, "LVDSTX driver is not loaded.\n");
		goto release_lvdstx;
	}

	ret = npsc01_lvdstx_clock_init(pdev);
	if(ret) {
		lvds_err("Failed to get lvds clocks.\n");
		goto release_lvdstx;
	}

	ret = npsc01_lvdstx_clock_enable(drv_data);
	if(ret) {
		lvds_err("Failed to enable lvdstx clocks.\n");
		goto release_lvdstx;
	}

	ret = npsc01_lvdstx_get_resource(pdev);
	if(ret) {
		lvds_err("Failed to get lvdstx resources.\n");
		goto release_lvdstx;
	}

	npsc01_lvdstx_ctrl_enable(drv_data);
	ret = npsc01_lvdstx_notifier_register(drv_data);
	if(ret) {
		lvds_err("Failed to register notifier.\n");
		goto release_lvdstx;
	}

	INIT_WORK(&drv_data->resume_work, npsc01_lvdstx_resume_work);
	device_enable_async_suspend(&pdev->dev);

	drv_data->lvdstx_enabled = 1;

	LVDS_CUR_POSITION;
	return 0;

release_lvdstx:
	return ret;
}

static int npsc01_lvdstx_remove(struct platform_device *pdev)
{
	LVDS_CUR_POSITION;
	return 0;
}

static const struct of_device_id lvdstx_of_match[] = {
	{ .compatible = "nufront,lvdstx", },
	{},
};
MODULE_DEVICE_TABLE(of, lvdstx_of_match);

static struct platform_driver npsc01_lvdstx_driver =
{
	.probe    = npsc01_lvdstx_probe,
	.remove    = npsc01_lvdstx_remove,
	.suspend  = npsc01_lvdsx_plat_suspend,
	.resume   = npsc01_lvdstx_plat_resume,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(lvdstx_of_match),
	},
};

static int __init npsc01_lvdstx_init(void)
{
	return platform_driver_register(&npsc01_lvdstx_driver);
}

static void __exit npsc01_lvdstx_exit(void)
{
	platform_driver_unregister(&npsc01_lvdstx_driver);
}

module_init(npsc01_lvdstx_init);
module_exit(npsc01_lvdstx_exit);
