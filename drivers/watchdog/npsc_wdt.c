/*
 * Copyright 2010-2011 Picochip Ltd., Jamie Iles
 * http://www.picochip.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/moduleparam.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/watchdog.h>
#include <linux/delay.h>

#ifndef FPGA_PLATFORM_NPSC01
//#define FPGA_PLATFORM_NPSC01
#endif


#define WDOG_CONTROL_REG_OFFSET		    0x00
#define WDOG_CONTROL_REG_WDT_EN_MASK	    0x03
#define WDOG_CONTROL_REG_WDT_EN_MASK_ONLY   0x01
#define WDOG_TIMEOUT_RANGE_REG_OFFSET	    0x04
#define WDOG_CURRENT_COUNT_REG_OFFSET	    0x08
#define WDOG_COUNTER_RESTART_REG_OFFSET     0x0c
#define WDOG_COUNTER_RESTART_KICK_VALUE	    0x76
#define WDOG_COUNTER_REG_EOI_OFFSET 	    0x14

/* The maximum TOP (timeout period) value that can be set in the watchdog. */
#define NPSC_WDT_MAX_TOP		15

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started "
		 "(default=" __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define WDT_TIMEOUT		(HZ / 2)

static struct {
	spinlock_t		lock;
	void __iomem		*regs;
	struct clk		*clk;
	unsigned long		in_use;
	unsigned long		next_heartbeat;
	struct timer_list	timer;
	int			expect_close;
	u32			control;
	u32			timeout;
} npsc_wdt;

static inline int npsc_wdt_is_enabled(void)
{
	return readl(npsc_wdt.regs + WDOG_CONTROL_REG_OFFSET) &
		WDOG_CONTROL_REG_WDT_EN_MASK_ONLY;
}

static inline int npsc_wdt_top_in_seconds(unsigned top)
{
	/*
	 * There are 16 possible timeout values in 0..15 where the number of
	 * cycles is 2 ^ (16 + i) and the watchdog counts down.
	 */
#ifdef FPGA_PLATFORM_NPSC01
        return (1 << (16 + top)) / 24000000;
#else
	return (1 << (16 + top)) / clk_get_rate(npsc_wdt.clk);
#endif
}

static int npsc_wdt_get_top(void)
{
	int top = readl(npsc_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET) & 0xF;

	return npsc_wdt_top_in_seconds(top);
}

static inline void npsc_wdt_set_next_heartbeat(void)
{
	npsc_wdt.next_heartbeat = jiffies + npsc_wdt_get_top() * HZ;
}

static int npsc_wdt_set_top(unsigned top_s)
{
	int i, top_val = NPSC_WDT_MAX_TOP;

	/*
	 * Iterate over the timeout values until we find the closest match. We
	 * always look for >=.
	 */
	for (i = 0; i <= NPSC_WDT_MAX_TOP; ++i)
		if (npsc_wdt_top_in_seconds(i) >= top_s) {
			top_val = i;
			break;
		}

	/* Set the new value in the watchdog. */
	writel(((top_val<<4)|top_val), npsc_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);

	npsc_wdt_set_next_heartbeat();

	return npsc_wdt_top_in_seconds(top_val);
}

static void npsc_wdt_keepalive(void)
{
	writel(WDOG_COUNTER_RESTART_KICK_VALUE, npsc_wdt.regs +
	       WDOG_COUNTER_RESTART_REG_OFFSET);
}

static void npsc_wdt_ping(unsigned long data)
{
	if (time_before(jiffies, npsc_wdt.next_heartbeat) ||
	    (!nowayout && !npsc_wdt.in_use)) {
		npsc_wdt_keepalive();
		mod_timer(&npsc_wdt.timer, jiffies + WDT_TIMEOUT);
	} else
		pr_crit("keepalive missed, machine will reset\n");
}

static int npsc_wdt_open(struct inode *inode, struct file *filp)
{
	if (test_and_set_bit(0, &npsc_wdt.in_use))
		return -EBUSY;

	/* Make sure we don't get unloaded. */
	__module_get(THIS_MODULE);

	spin_lock(&npsc_wdt.lock);
	if (!npsc_wdt_is_enabled()) {
		/*
		 * The watchdog is not currently enabled. Set the timeout to
		 * the maximum and then start it.
		 */
		npsc_wdt_set_top(NPSC_WDT_MAX_TOP);
		writel(WDOG_CONTROL_REG_WDT_EN_MASK,
		       npsc_wdt.regs + WDOG_CONTROL_REG_OFFSET);
		npsc_wdt_keepalive();
	}

	npsc_wdt_set_next_heartbeat();

	spin_unlock(&npsc_wdt.lock);

	return nonseekable_open(inode, filp);
}

ssize_t npsc_wdt_write(struct file *filp, const char __user *buf, size_t len,
		     loff_t *offset)
{
	if (!len)
		return 0;

	if (!nowayout) {
		size_t i;

		npsc_wdt.expect_close = 0;

		for (i = 0; i < len; ++i) {
			char c;

			if (get_user(c, buf + i))
				return -EFAULT;

			if (c == 'V') {
				npsc_wdt.expect_close = 1;
				break;
			}
		}
	}

	npsc_wdt_set_next_heartbeat();

#ifndef FPGA_PLATFORM_NPSC01
	mod_timer(&npsc_wdt.timer, jiffies + WDT_TIMEOUT);
#endif

	return len;
}

static u32 npsc_wdt_time_left(void)
{
#ifdef FPGA_PLATFORM_NPSC01
	return readl(npsc_wdt.regs + WDOG_CURRENT_COUNT_REG_OFFSET) /
		24000000;
#else
	return readl(npsc_wdt.regs + WDOG_CURRENT_COUNT_REG_OFFSET) /
		clk_get_rate(npsc_wdt.clk);
#endif
}

static const struct watchdog_info npsc_wdt_ident = {
	.options	= WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE,
	.identity	= "NPSC Watchdog",
};

static long npsc_wdt_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	int timeout;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user((struct watchdog_info *)arg, &npsc_wdt_ident,
				    sizeof(npsc_wdt_ident)) ? -EFAULT : 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *)arg);

	case WDIOC_KEEPALIVE:
		npsc_wdt_set_next_heartbeat();

		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		timeout = npsc_wdt_set_top(val);
		return put_user(timeout , (int __user *)arg);

	case WDIOC_GETTIMEOUT:
		return put_user(npsc_wdt_get_top(), (int __user *)arg);

	case WDIOC_GETTIMELEFT:
		/* Get the time left until expiry. */
		if (get_user(val, (int __user *)arg))
			return -EFAULT;
		return put_user(npsc_wdt_time_left(), (int __user *)arg);

	default:
		return -ENOTTY;
	}
}

static int npsc_wdt_release(struct inode *inode, struct file *filp)
{
	clear_bit(0, &npsc_wdt.in_use);

	if (!npsc_wdt.expect_close) {
		del_timer(&npsc_wdt.timer);

		if (!nowayout)
			pr_crit("unexpected close, system will reboot soon\n");
		else
			pr_crit("watchdog cannot be disabled, system will reboot soon\n");
	}

	npsc_wdt.expect_close = 0;

	return 0;
}

#ifdef CONFIG_PM
static int npsc_wdt_suspend(struct device *dev)
{
	npsc_wdt.control = readl(npsc_wdt.regs + WDOG_CONTROL_REG_OFFSET);
	npsc_wdt.timeout = readl(npsc_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);

	clk_disable(npsc_wdt.clk);

	return 0;
}

static int npsc_wdt_resume(struct device *dev)
{
	int err = clk_enable(npsc_wdt.clk);

	if (err)
		return err;

	writel(npsc_wdt.timeout, npsc_wdt.regs + WDOG_TIMEOUT_RANGE_REG_OFFSET);
	writel(npsc_wdt.control, npsc_wdt.regs + WDOG_CONTROL_REG_OFFSET);

	npsc_wdt_keepalive();

	return 0;
}

static const struct dev_pm_ops npsc_wdt_pm_ops = {
	.suspend	= npsc_wdt_suspend,
	.resume		= npsc_wdt_resume,
};
#endif /* CONFIG_PM */

static const struct file_operations wdt_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.open		= npsc_wdt_open,
	.write		= npsc_wdt_write,
	.unlocked_ioctl	= npsc_wdt_ioctl,
	.release	= npsc_wdt_release
};

static struct miscdevice npsc_wdt_miscdev = {
	.fops		= &wdt_fops,
	.name		= "watchdog",
	.minor		= WATCHDOG_MINOR,
};

struct a9_wdt_driver_info {
	struct device *dev;
	unsigned int a9_wdt_irq;
};

/**
	clear interrupt EOI behavior is same as petting Watchdog so disable
	interrupt service rountine from probe.
*/
//static irqreturn_t a9_wdt_irq_handler(int irq, void *data)
//{
//
//	//readl(npsc_wdt.regs + WDOG_COUNTER_REG_EOI_OFFSET);
//	pr_crit("%s:%s:%d\n", __FILE__, __func__, __LINE__);
//
//	return IRQ_HANDLED;
//}


static int npsc_wdt_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct a9_wdt_driver_info *wdt_info;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	wdt_info = kzalloc(sizeof(*wdt_info), GFP_KERNEL);
        if (wdt_info == NULL) {
                return -ENOMEM;
        }
        wdt_info->dev = &pdev->dev;

	npsc_wdt.regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(npsc_wdt.regs))
		return PTR_ERR(npsc_wdt.regs);

#ifndef FPGA_PLATFORM_NPSC01
	npsc_wdt.clk = clk_get(&pdev->dev, "wdt_clk");
	if (IS_ERR(npsc_wdt.clk)) {
		return PTR_ERR(npsc_wdt.clk);
	}

	ret = clk_prepare_enable(npsc_wdt.clk);
	//ret = clk_enable(npsc_wdt.clk);
	if (ret) {
		goto out_put_clk;
	}
#endif
	spin_lock_init(&npsc_wdt.lock);

	ret = misc_register(&npsc_wdt_miscdev);
	if (ret)
		goto out_disable_clk;

/**
	wdt_info->a9_wdt_irq = platform_get_irq(pdev, 0);
	if(wdt_info->a9_wdt_irq == -ENXIO) {
		ret = -ENXIO;
		goto fail1;
	}

	ret = devm_request_irq(&pdev->dev, wdt_info->a9_wdt_irq,
			a9_wdt_irq_handler,
			IRQF_TRIGGER_HIGH, "a9wdt_reset", wdt_info);

	if (ret) {
		ret = -ENXIO;
		goto fail1;
	}
*/

	platform_set_drvdata(pdev, wdt_info);

#ifndef FPGA_PLATFORM_NPSC01
	npsc_wdt_set_next_heartbeat();
	setup_timer(&npsc_wdt.timer, npsc_wdt_ping, 0);
	mod_timer(&npsc_wdt.timer, jiffies + WDT_TIMEOUT);
#endif

	return 0;
fail1:
	if(npsc_wdt.regs)
        iounmap(npsc_wdt.regs);
	if(wdt_info)
		kfree(wdt_info);

out_disable_clk:
	clk_disable(npsc_wdt.clk);
out_put_clk:
	clk_put(npsc_wdt.clk);

	return ret;
}

static int npsc_wdt_drv_remove(struct platform_device *pdev)
{
	struct a9_wdt_driver_info *wdt_info;

	wdt_info = (struct a9_wdt_driver_info *)platform_get_drvdata(pdev);

	misc_deregister(&npsc_wdt_miscdev);

	clk_disable(npsc_wdt.clk);
	clk_put(npsc_wdt.clk);

	if(npsc_wdt.regs)
                iounmap(npsc_wdt.regs);
	if(wdt_info)
		kfree(wdt_info);

	return 0;
}

static struct of_device_id a9_wdt_ids[] = {
        { .compatible = "npsc,npsc-a9wakeup-wdt", },
        { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, a9_wdt_ids);

static struct platform_driver npsc_wdt_driver = {
	.probe		= npsc_wdt_drv_probe,
	.remove		= npsc_wdt_drv_remove,
	.driver		= {
		.name	= "npsc_wdt",
		.owner	= THIS_MODULE,
		.of_match_table = a9_wdt_ids,
#ifdef CONFIG_PM
		.pm	= &npsc_wdt_pm_ops,
#endif /* CONFIG_PM */
	},
};

module_platform_driver(npsc_wdt_driver);

MODULE_AUTHOR("Nufront");
MODULE_DESCRIPTION("Nufront Watchdog Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
