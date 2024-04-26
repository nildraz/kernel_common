/*
 *  Copyright (C) 2018 NUFRONT Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/bitops.h>

#include "efuse.h"

#define EFUSE_MAX_CAPACITY	128

#define SMIC40LL_STAT 		0x00
#define SMIC40LL_REQ 		0x08
#define SMIC40LL_ADDR 		0x0C
#define SMIC40LL_DATA 		0x10
#define SMIC40LL_INT_PEND 	0x20
#define SMIC40LL_INT_MSTAT 	0x24
#define SMIC40LL_INT_MASK 	0x28
#define SMIC40LL_INT_UNMASK 	0x2C

#define SMIC40LL_STAT_POWER	0x0
#define SMIC40LL_STAT_PROGM	0x1
#define SMIC40LL_STAT_LOAD	0x2
#define SMIC40LL_STAT_STANDBY	0x3
#define SMIC40LL_STAT_READ	0x4

struct smic_efuse_data {
	struct efusedevice edev;
	void __iomem *regs;
};

#define to_efuse_data(x) container_of(x, struct smic_efuse_data, edev)

/*
 * Efuse_AVDD_Control
 *
 * It is necessary to provide 2.5v power supply to AVDD during programming
 * efuse, and set AVDD to 0v or ground after the end of programming. This
 * function needs to be implemented by the board user.
 */
static void Efuse_AVDD_Control(struct smic_efuse_data *efuse_data, bool enable)
{
	/* nothing to do */
}

static int smic_efuse_wait_for(struct smic_efuse_data *efuse_data, u32 stat)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(5);

	do {
		if (readl(efuse_data->regs + SMIC40LL_STAT) == stat)
			return 0;
	} while (time_is_after_jiffies(timeout));

	return -ETIMEDOUT;
}

static int smic_efuse_read_byte(struct efusedevice *edev, char *val, u32 offset)
{
	struct smic_efuse_data *efuse_data = to_efuse_data(edev);

	writel(offset, efuse_data->regs + SMIC40LL_ADDR);
	writel(0x1, efuse_data->regs + SMIC40LL_REQ);
	if (smic_efuse_wait_for(efuse_data, SMIC40LL_STAT_STANDBY))
		return -EIO;

	*val = (char)readl(efuse_data->regs + SMIC40LL_DATA);
	return 0;
}

static int smic_efuse_program_byte(struct efusedevice *edev,
	const char val, u32 offset)
{
	struct smic_efuse_data *efuse_data = to_efuse_data(edev);
	unsigned long bitmap = val;
	u32 bit_pos;
	int res = 0;
	u32 i;

	for_each_set_bit(i, &bitmap, BITS_PER_BYTE) {
		bit_pos = i*128 + offset;
		if (smic_efuse_wait_for(efuse_data, SMIC40LL_STAT_STANDBY)) {
			res = -EIO;
			break;
		}
		writel(bit_pos, efuse_data->regs + SMIC40LL_ADDR);
		writel(0x2, efuse_data->regs + SMIC40LL_REQ);
	}

	return res;
}

static void smic_efuse_pre_program(struct efusedevice *edev)
{
	struct smic_efuse_data *efuse_data = to_efuse_data(edev);

	Efuse_AVDD_Control(efuse_data, true);
}

static void smic_efuse_post_program(struct efusedevice *edev)
{
	struct smic_efuse_data *efuse_data = to_efuse_data(edev);

	Efuse_AVDD_Control(efuse_data, false);
}

/*
 * smic_efuse_getwp
 *
 * This function provides write protection for the device. If user wants the
 * device to be readonly mode, return true.
 */
static bool smic_efuse_getwp(struct efusedevice *edev)
{
	return false;
}

static struct efuse_ops smic_efuse_ops = {
	.read_byte	= smic_efuse_read_byte,
	.program_byte	= smic_efuse_program_byte,
	.pre_program	= smic_efuse_pre_program,
	.post_program	= smic_efuse_post_program,
	.getwp		= smic_efuse_getwp,
};

static int smic_efuse_probe(struct platform_device *pdev)
{
	struct smic_efuse_data *efuse_data;
	struct resource *mem, *ioarea;
	int ret;

	efuse_data = kzalloc(sizeof(struct smic_efuse_data), GFP_KERNEL);
	if (!efuse_data) {
		ret = -ENOMEM;
		goto err_kfree;
	}

	/* Get basic io resource and map it */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "Efuse no mem resource?\n");
		ret = -EINVAL;
		goto err_kfree;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "IO region already claimed\n");
		ret = -EBUSY;
		goto err_kfree;
	}

	efuse_data->regs = ioremap_nocache(mem->start, resource_size(mem));
	if (!efuse_data->regs) {
		dev_err(&pdev->dev, "IO region already mapped\n");
		ret = -ENOMEM;
		goto err_kfree;
	}

	efuse_data->edev.capacity = EFUSE_MAX_CAPACITY;
	efuse_data->edev.ops = &smic_efuse_ops;
	ret = efuse_register(&efuse_data->edev);
	if (ret)
		goto err_unmap;

	platform_set_drvdata(pdev, efuse_data);
	return 0;

err_unmap:
	iounmap(efuse_data->regs);
err_kfree:
	kfree(efuse_data);
	return ret;
}

static int smic_efuse_remove(struct platform_device *pdev)
{
	struct smic_efuse_data *efuse_data = platform_get_drvdata(pdev);

	efuse_unregister(&efuse_data->edev);
	kfree(efuse_data);
	return 0;
}

static const struct of_device_id smic_efuse_of_match[] = {
	{ .compatible = "smic,smic40ll-efuse" },
	{},
};

static struct platform_driver smic_efuse_driver = {
	.probe		= smic_efuse_probe,
	.remove		= smic_efuse_remove,
	.driver		= {
		.name	= "efuse_plat",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(smic_efuse_of_match),
	},
};

module_platform_driver(smic_efuse_driver);

MODULE_AUTHOR("wang jingyang <jingyang.wang@nufront.com>");
MODULE_DESCRIPTION("Device driver interface for Efuse.");
MODULE_LICENSE("GPL");
