/*
 * npsc I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 *
 * ----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/of_i2c.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/pinctrl/consumer.h>
#include "i2c-npsc-core.h"

#define		I2C_DEFAULT_CLK		(192000000)
static struct i2c_algorithm i2c_npsc_algo = {
	.master_xfer	= i2c_npsc_xfer,
	.functionality	= i2c_npsc_func,
};
static u32 i2c_npsc_get_clk_rate_khz(struct npsc_i2c_dev *dev)
{
	if(!IS_ERR(dev->clk)) {
		return clk_get_rate(dev->clk)/1000;
	}
	else
		return (I2C_DEFAULT_CLK / 1000);
}

#ifdef CONFIG_ACPI
static int npsc_i2c_acpi_configure(struct platform_device *pdev)
{
	struct npsc_i2c_dev *dev = platform_get_drvdata(pdev);

	if (!ACPI_HANDLE(&pdev->dev))
		return -ENODEV;

	dev->adapter.nr = -1;
	dev->tx_fifo_depth = 32;
	dev->rx_fifo_depth = 32;
	return 0;
}

static const struct acpi_device_id npsc_i2c_acpi_match[] = {
	{ "INT33C2", 0 },
	{ "INT33C3", 0 },
	{ "80860F41", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, npsc_i2c_acpi_match);
#else
static inline int npsc_i2c_acpi_configure(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif

static int npsc_i2c_probe(struct platform_device *pdev)
{
	struct npsc_i2c_dev *dev;
	struct i2c_adapter *adap;
	struct resource *mem;
	struct pinctrl *pinctrl;
	int irq, r;
	u32 val;
	struct device_node *np = pdev->dev.of_node;
	struct clk *clk;
	/* NOTE: driver uses the static register mapping */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -EINVAL;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq; /* -ENXIO */
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(struct npsc_i2c_dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	init_completion(&dev->cmd_complete);
	mutex_init(&dev->lock);
	dev->dev = &pdev->dev;
	dev->irq = irq;
	platform_set_drvdata(pdev, dev);
	clk = devm_clk_get(&pdev->dev, "i2c_p_clk");
	if (!IS_ERR(clk))
		clk_prepare_enable(clk);
	else {
		dev_err(&pdev->dev, "no i2c_p_clk\n");
	}

	dev->clk = devm_clk_get(&pdev->dev, "i2c_clk");
	dev->get_clk_rate_khz = i2c_npsc_get_clk_rate_khz;
	if (!IS_ERR(dev->clk)){
		clk_set_rate(dev->clk,64000000);
		clk_prepare_enable(dev->clk);
	}
	else
		dev_err(&pdev->dev, "no i2c_clk\n");

	dev->functionality =
		I2C_FUNC_I2C |
		I2C_FUNC_10BIT_ADDR |
		I2C_FUNC_SMBUS_BYTE |
		I2C_FUNC_SMBUS_BYTE_DATA |
		I2C_FUNC_SMBUS_WORD_DATA |
		I2C_FUNC_SMBUS_I2C_BLOCK;
	if (of_property_read_u32(np, "speed", &val)) {
		printk(KERN_ERR"I2C can not set speed\n");
		r = -EINVAL;
		goto err_release;
	}

	dev->master_cfg =  NPSC_IC_CON_MASTER | NPSC_IC_CON_SLAVE_DISABLE |
		NPSC_IC_CON_RESTART_EN | (val ? NPSC_IC_CON_SPEED_FAST : NPSC_IC_CON_SPEED_STD);

	if (of_property_read_u32(np, "id", &val)) {
		printk(KERN_ERR"I2C can not get id\n");
		r = -EINVAL;
		goto err_release;
	}
	pdev->id = val;
#ifdef	CONFIG_PINCTRL_NUFRONT
//	if(dev->dev->pins)
//		dev->pin_ctrl = dev->dev->pins->p;
//	if(!dev->pin_ctrl) {
//		dev_err(&pdev->dev,"I2C can not get pinctrl\n");
//		r = -EINVAL;
//		goto err_release;
//	}
	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if(IS_ERR(pinctrl)) {
		dev_warn(&pdev->dev, "pins are not configured from the driver!!\n");
		return -EINVAL;
	}

#endif
	/* Try first if we can configure the device from ACPI */
	r = npsc_i2c_acpi_configure(pdev);
	if (r) {
		u32 param1 = i2c_npsc_read_comp_param(dev);

		dev->tx_fifo_depth = ((param1 >> 16) & 0xff) + 1;
		dev->rx_fifo_depth = ((param1 >> 8)  & 0xff) + 1;
		dev->adapter.nr = pdev->id;
	}
	r = i2c_npsc_init(dev);
	if (r)
		goto err_release;


	i2c_npsc_disable_int(dev);
	r = devm_request_irq(&pdev->dev, dev->irq, i2c_npsc_isr, IRQF_SHARED|IRQF_NO_SUSPEND,
			pdev->name, dev);
	if (r) {
		dev_err(&pdev->dev, "failure requesting irq %i\n", dev->irq);
		goto err_release;
	}

	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	strlcpy(adap->name, "npsc I2C adapter",
			sizeof(adap->name));
	adap->algo = &i2c_npsc_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	r = i2c_add_numbered_adapter(adap);
	if (r) {
		dev_err(&pdev->dev, "failure adding adapter\n");
		goto err_release;
	}
	of_i2c_register_devices(adap);
	acpi_i2c_register_devices(adap);

	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	dev->suspend_flag = 0;

	return 0;

err_release:
	dev_err(&pdev->dev,"i2c probe failure\n");
	if(!IS_ERR(dev->clk))
		clk_disable_unprepare(dev->clk);
	platform_set_drvdata(pdev,NULL);
	return r;
}

static int npsc_i2c_remove(struct platform_device *pdev)
{
	struct npsc_i2c_dev *dev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	i2c_del_adapter(&dev->adapter);

	i2c_npsc_disable(dev);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id npsc_i2c_of_match[] = {
	{ .compatible = "npsc,npsc-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, npsc_i2c_of_match);
#endif

#ifdef CONFIG_PM
static int npsc_i2c_suspend(struct device *dev)
{
#if 0
	struct platform_device *pdev = to_platform_device(dev);
	struct npsc_i2c_dev *i_dev = platform_get_drvdata(pdev);

	mutex_lock(&i_dev->lock);
	i2c_npsc_disable_int(i_dev);
	i_dev->suspend_flag = 1;
	mutex_unlock(&i_dev->lock);
#endif
	return 0;
}

int npsc_i2c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct npsc_i2c_dev *i_dev = platform_get_drvdata(pdev);

	mutex_lock(&i_dev->lock);
	i2c_npsc_init(i_dev);
	i_dev->suspend_flag = 0;
	i2c_npsc_disable_int(i_dev);
	mutex_unlock(&i_dev->lock);
	return 0;
}
#ifdef CONFIG_PM_RUNTIME
static int npsc_i2c_runtime_suspend(struct device *dev)
{
	struct npsc_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	if (!IS_ERR(i2c_dev->clk))
		clk_disable_unprepare(i2c_dev->clk);
	if(i2c_dev->pin_ctrl){
		devm_pinctrl_put(i2c_dev->pin_ctrl);
		i2c_dev->pin_ctrl = NULL;
	}
	return 0;
}
int npsc_i2c_runtime_resume(struct device *dev)
{
	struct npsc_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	if (!IS_ERR(i2c_dev->clk))
		clk_prepare_enable(i2c_dev->clk);
	if(i2c_dev->pin_ctrl == NULL)
		i2c_dev->pin_ctrl = devm_pinctrl_get_select_default(dev);
	return 0;
}
EXPORT_SYMBOL(npsc_i2c_runtime_resume);
static int npsc_i2c_runtime_idle(struct device *dev)
{
	dev_dbg(dev,"%s\n",__func__);
	return 0;
}
#endif
struct dev_pm_ops npsc_i2c_dev_pm_ops = {
	.suspend_noirq = npsc_i2c_suspend,
	.resume_noirq = npsc_i2c_resume,
	SET_RUNTIME_PM_OPS(npsc_i2c_runtime_suspend,npsc_i2c_runtime_resume,npsc_i2c_runtime_idle)
};
#endif

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:i2c_npsc");

static struct platform_driver npsc_i2c_driver = {
	.remove		= npsc_i2c_remove,
	.driver		= {
		.name	= "i2c_npsc",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(npsc_i2c_of_match),
		.acpi_match_table = ACPI_PTR(npsc_i2c_acpi_match),
#ifdef CONFIG_PM
		.pm	= &npsc_i2c_dev_pm_ops,
#endif
	},
};

static int __init npsc_i2c_init_driver(void)
{
	return platform_driver_probe(&npsc_i2c_driver, npsc_i2c_probe);
}
subsys_initcall(npsc_i2c_init_driver);

static void __exit npsc_i2c_exit_driver(void)
{
	platform_driver_unregister(&npsc_i2c_driver);
}
module_exit(npsc_i2c_exit_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("npsc I2C bus adapter");
MODULE_LICENSE("GPL");
