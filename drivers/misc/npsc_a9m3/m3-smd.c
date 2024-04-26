/*
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

#define DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include "m3-smd.h"

#define NUFRONT_PINCTRL_NO_IMPL

#if !defined NUFRONT_PINCTRL_NO_IMPL
//pinctrl for nufront
extern void nufront_scm_writel(unsigned int val, unsigned int reg_offset);
extern unsigned int nufront_scm_readl(unsigned int reg_offset);
#endif

static unsigned int modem_major;

//#ifdef NUFRONT_PINCTRL_NO_IMPL
//#undef NUFRONT_PINCTRL_NO_IMPL
//#endif

/*static void __iomem *testcode; */

#ifdef NUFRONT_PINCTRL_NO_IMPL
static void __iomem *CPU_TO_M3_IRQ_CTRL;
static void __iomem *CPU_TO_M3_IRQ_ENA;
static void __iomem *CPU_TO_M3_IRQ_EOI;
#else
static unsigned int CPU_TO_M3_IRQ_CTRL;
static unsigned int CPU_TO_M3_IRQ_ENA;
static unsigned int CPU_TO_M3_IRQ_EOI;
#endif

static volatile void __iomem *m3ctrl;
static volatile void __iomem *m3load; //M3S and M3P loading address

static spinlock_t m3s_intr_lock;
static spinlock_t m3p_intr_lock;

DECLARE_COMPLETION(m3_s_comp);
DECLARE_COMPLETION(m3_p_comp);
#define M3_DELAY	(10 * HZ)


#define CPU_TO_MCU_IRQ			0x380
#define CPU_TO_MCU_IRQ_ENA		0x384
#define CPU_TO_MCU_EOI			0x388
#define SCM_M3_BROMAPP_ADDR_P	0x1f0
#define SCM_M3_BROMAPP_ADDR_S	0x1f4
#define A9_RELEASE_M3_CTRL		0x05200210
#define A9_RELEASE_M3_PRINT		0x09
#define A9_RELEASE_M3_SCAN		0x12
#define A9_RESET_M3_PRINT		0x01
#define A9_RESET_M3_SCAN		0x02

#define PRCM_AVISION_PWR_STATE	0x08 // Bit 3 for PRMC_BUS_PWR_CTTRL
#define PRCM_AVISION_PWR_SLEEP	0x08 // Bit 3 for PRCM_BUS_PWR_STATUS
#define PRCM_BUS_PWR_CTRL		0x04 // Offset to the address A9_RELEASE_M3_CTRL
#define PRCM_BUS_PWR_STATUS		0x0c // Offset to the address A9_RELEASE_M3_CTRL

#define SCM_BASE_M3				0x052101f0

static int m3_ctrl(struct m3_smd_driver_info *info, int release, int printer);
static int reset_avision_subsys(struct m3_smd_driver_info *info);
static unsigned int checksum32(void *addr, unsigned int size);

/**
static int check_memtag(struct m3_shm_head *shm_hd, int m3_id)
{
	if (m3_id) {
		if (shm_hd->tag[0] != 'P' || shm_hd->tag[1] != 'R'
				|| shm_hd->tag[2] != 'I'
				|| shm_hd->tag[3] != 'N') {
			return -1;
		}
	} else {
		if (shm_hd->tag[0] != 'S' || shm_hd->tag[1] != 'C'
				|| shm_hd->tag[2] != 'A'
				|| shm_hd->tag[3] != 'N') {
			return -1;
		}
	}

	return 0;
}
*/

static void clear_irq_from_m3(int irq, struct m3_smd_driver_info *dinfo)
{
#ifdef NUFRONT_PINCTRL_NO_IMPL
	if (irq == dinfo->m3s_2ap_irq0) {
		pr_err("M3 Scan's Interrupt0 is CLEARED\n");
		writel(readl(CPU_TO_M3_IRQ_EOI) | (1 << 2), CPU_TO_M3_IRQ_EOI);

	} else if (irq == dinfo->m3s_2ap_irq1) {
		pr_err("M3 Scan's Interrupt1 is CLEARED\n");
		writel(readl(CPU_TO_M3_IRQ_EOI) | (1 << 3), CPU_TO_M3_IRQ_EOI);

	} else if (irq == dinfo->m3p_2ap_irq0) {
		pr_err("M3 Printer's Interruptr0 is CLEARED\n");
		writel(readl(CPU_TO_M3_IRQ_EOI) | (1 << 0), CPU_TO_M3_IRQ_EOI);
	} else if (irq == dinfo->m3p_2ap_irq1) {
		pr_err("M3 Printer's Interrupt1 is CLEARED\n");
		writel(readl(CPU_TO_M3_IRQ_EOI) | (1 << 1), CPU_TO_M3_IRQ_EOI);
	}
#else
	if (irq == dinfo->m3s_2ap_irq0) {
		pr_err("M3 Scan's Interrupt0 is CLEARED\n");
		nufront_scm_writel(nufront_scm_readl(CPU_TO_M3_IRQ_EOI) | (1 << 2), CPU_TO_M3_IRQ_EOI);
	} else if (irq == dinfo->m3s_2ap_irq1) {
		pr_err("M3 Scan's Interrupt1 is CLEARED offset %d\n", CPU_TO_M3_IRQ_EOI);
		nufront_scm_writel(nufront_scm_readl(CPU_TO_M3_IRQ_EOI) | (1 << 3), CPU_TO_M3_IRQ_EOI);

	} else if (irq == dinfo->m3p_2ap_irq0) {
		pr_err("M3 Printer's Interruptr0 is CLEARED\n");
		nufront_scm_writel(nufront_scm_readl(CPU_TO_M3_IRQ_EOI) | (1 << 0), CPU_TO_M3_IRQ_EOI);
	} else if (irq == dinfo->m3p_2ap_irq1) {
		pr_err("M3 Printer's Interrupt1 is CLEARED\n");
		nufront_scm_writel(nufront_scm_readl(CPU_TO_M3_IRQ_EOI) | (1 << 1), CPU_TO_M3_IRQ_EOI);
	} else if (irq == dinfo->m3p_wdt_irq) {
		pr_err("***a***M3 Printer's wdt is timeout then to reset M3P\n");
		m3_ctrl(dinfo, RESET, PRINTER);
	} else if (irq == dinfo->m3s_wdt_irq) {
		pr_err("M3 Scanner's wdt is timeout then to reset M3S\n");
		m3_ctrl(dinfo, RESET, SCANNER);
	}


#endif
}

static irqreturn_t m3_2ap_irq_handler(int irq, void *data)
{
/* int m3_id; */
/*	unsigned char *ptr; */
/*	struct m3_shm_head *shm_hd; */
	struct m3_smd_driver_info *dinfo = (struct m3_smd_driver_info *)data;
	pr_err("m3p_2ap_irq_handler():  irq = %d\n", irq);

	clear_irq_from_m3(irq, dinfo);
/*

	if (irq == dinfo->m3s_2ap_irq0) {
		m3_id = 0;
	} else if (irq == dinfo->m3p_2ap_irq0) {
		m3_id = 1;
	} else {
		pr_err("Unknown IRQ\n");
		return IRQ_HANDLED;
	}

	if (m3_id != dinfo->curr_m3) {
		pr_err("m3_id is %d, but curr_m3 is %d\n", m3_id, dinfo->curr_m3);
		return IRQ_HANDLED;
	}

	if (m3_id) {
		writel(0x3, CPU_TO_M3_IRQ_EOI);
		ptr = dinfo->m3_p_smd_base;
	} else {
		writel((0x3 << 2), CPU_TO_M3_IRQ_EOI);
		ptr = dinfo->m3_s_smd_base;
	}
	shm_hd = (struct m3_shm_head *)ptr;
	ptr += sizeof(struct m3_shm_head);

	pr_info("TAG IS: %c%c%c%c", shm_hd->tag[0], shm_hd->tag[1],
			shm_hd->tag[2], shm_hd->tag[3]);

	if (check_memtag(shm_hd, m3_id)) {
		pr_err("Curr m3 does not matched his TAG\n");
		return IRQ_HANDLED;
	}

	if (shm_hd->cksum != checksum32((void *)ptr, shm_hd->length)) {
		pr_err("Mem CheckSum Failed!\n");
		return IRQ_HANDLED;
	} else {
		pr_err("Mem CheckSum Success");
	}

	if (m3_id)
		complete(&m3_p_comp);
	else
		complete(&m3_s_comp);
*/

	return IRQ_HANDLED;
}

static DEFINE_MUTEX(isa_lock);
static int m3_smd_open(struct inode *inode, struct file *filp)
{
	int err = 0;
	struct device *dev;
	struct m3_smd_driver_info *dinfo = container_of(
			inode->i_cdev,
			struct m3_smd_driver_info,
			cdev);
	dev = dinfo->dev;
	dev_info(dev, "Enter %s()\n", __func__);
	dev_info(dev, "smd base M3_S: %p, M3_P: %p\n",
			dinfo->m3_s_smd_base,
			dinfo->m3_p_smd_base);

	mutex_lock(&isa_lock);

	atomic_inc(&dinfo->is_open);

	dev_info(dev, "%s(): is_open = 0x%0x\n", __func__,
			atomic_read(&dinfo->is_open));

	if (!atomic_dec_and_test(&dinfo->is_open)) {
		dev_err(dev, "%s(): Device had opened yet\n", __func__);
		mutex_unlock(&isa_lock);
		return -ENODEV;
	}

	dinfo->curr_m3 = -1;

	if (filp != NULL)
		filp->private_data = dinfo;
	mutex_unlock(&isa_lock);

	dev_info(dev, "Exit %s()\n", __func__);
	return err;
}

static int m3_smd_close(struct inode *inode, struct file *filp)
{
	long err = 0;
	struct m3_smd_driver_info *dinfo = filp->private_data;

	atomic_set(&dinfo->is_open, 0);

	dev_info(dinfo->dev, "%s(): is_open = 0x%0x\n",
			__func__, atomic_read(&dinfo->is_open));
	return err;
}

ssize_t m3_smd_write(struct file *filep, const char __user *buf,
		size_t len, loff_t *ppos)
{
	return -EFAULT;
}

ssize_t m3_smd_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	return -EFAULT;
}

static unsigned int checksum32(void *addr, unsigned int size)
{
	unsigned int sum;

	sum = 0;
	while (size >= 4) {
		sum  += * (unsigned int *) addr;
		addr += 4;
		size -= 4;
	}
	switch(size) {
		case 3:
			sum += (*(unsigned char *)(2+addr)) << 16;
		case 2:
			sum += (*(unsigned char *)(1+addr)) << 8;
		case 1:
			sum += (*(unsigned char *)(0+addr));
	}
	return sum;
}



static int send_intr_2m3(struct m3_smd_driver_info *dinfo)
{
	int ret = 0;
	unsigned long flags;
    unsigned int m3_id;

	m3_id = dinfo->curr_m3;

	if (m3_id < 0) {
		dev_err(dinfo->dev, "Curr_M3 is -1!\n");
		return -1;
	}

	if (m3_id) {
		spin_lock_irqsave(&m3p_intr_lock, flags);
#ifdef NUFRONT_PINCTRL_NO_IMPL
		writel(0x3, CPU_TO_M3_IRQ_ENA);
		writel(0x1, CPU_TO_M3_IRQ_CTRL);
#else
		nufront_scm_writel(0x3, CPU_TO_M3_IRQ_ENA);
		nufront_scm_writel(0x1, CPU_TO_M3_IRQ_CTRL);
#endif
		spin_unlock_irqrestore(&m3p_intr_lock, flags);
	} else {
		spin_lock_irqsave(&m3s_intr_lock, flags);
#ifdef NUFRONT_PINCTRL_NO_IMPL
		writel((0x3 << 2), CPU_TO_M3_IRQ_ENA);
		writel((0x1 << 2), CPU_TO_M3_IRQ_CTRL);
#else
		nufront_scm_writel((0x3 << 2), CPU_TO_M3_IRQ_ENA);
		nufront_scm_writel((0x1 << 2), CPU_TO_M3_IRQ_CTRL);
#endif
		spin_unlock_irqrestore(&m3s_intr_lock, flags);
	}

	if (m3_id)
		ret = wait_for_completion_timeout(&m3_p_comp, M3_DELAY);
	else
		ret = wait_for_completion_timeout(&m3_s_comp, M3_DELAY);

	if (!ret) {
		dev_err(dinfo->dev, "Wait %s for complete timeout!\n",
				m3_id ? "M3_P" : "M3_S");
		return -1;
	}

	return 0;
}

static void ap2m3_int_shm_test(struct m3_smd_driver_info *dinfo)
{
	unsigned long i = 0;
	unsigned char *ptr;
	unsigned int m3_id = dinfo->curr_m3;
	struct m3_shm_head shm_hd;

	if (m3_id < 0) {
		dev_err(dinfo->dev, "Curr_M3 is -1!\n");
		return;
	}

	if (m3_id) {
		sprintf(&shm_hd.tag[0], "PRIN");
		shm_hd.length = 1024;
		ptr = (unsigned char *)dinfo->m3_p_smd_base + sizeof(shm_hd);
	} else {
		sprintf(&shm_hd.tag[0], "SCAN");
		shm_hd.length = 1024;
		ptr = (unsigned char *)dinfo->m3_s_smd_base + sizeof(shm_hd);
	}

	for(i = 0; i < shm_hd.length; i++) {
		get_random_bytes(&ptr[i], 1);
	}

	shm_hd.cksum = checksum32(ptr, shm_hd.length);

	ptr -= sizeof(shm_hd);

	memcpy(ptr, &shm_hd, sizeof(shm_hd));

	send_intr_2m3(dinfo);
}

static long m3_smd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long err = 0;
	struct m3_smd_driver_info *dinfo = filp->private_data;

	printk("**************abc*************\n");
	if (arg < 0 || arg > 1) {
		dinfo->curr_m3 = arg;
		dev_err(dinfo->dev, "UnKnown m3, Please select correct m3\n");
	//	return -1;
	}


	dev_err(dinfo->dev, "cmd 0x%08x arg 0x%ld\n", cmd, arg);
	switch (cmd) {
	case M3_INT_SHM_TEST:
		ap2m3_int_shm_test(dinfo);
		break;
#if !defined NUFRONT_PINCTRL_NO_IMPL
	case M3IOC_SETM3P_LOADADDR:
		dev_err(dinfo->dev, "arg = %d printer load address 0x%x\n", arg, SCM_M3_BROMAPP_ADDR_P);
		nufront_scm_writel(arg, SCM_M3_BROMAPP_ADDR_P);
		break;
	case M3IOC_SETM3S_LOADADDR:
		dev_err(dinfo->dev, "arg = %d scanner load address 0x%x\n", arg, SCM_M3_BROMAPP_ADDR_S);
		nufront_scm_writel(arg, SCM_M3_BROMAPP_ADDR_S);
		break;
#else
	case M3IOC_SETM3P_LOADADDR:
		dev_dbg(dinfo->dev, "setting m3p loading address");
		writel(arg, m3load);
		break;
	case M3IOC_SETM3S_LOADADDR:
		dev_dbg(dinfo->dev, "setting m3s loading address");
		writel(arg, m3load+0x04);
		break;
#endif
	case M3IOC_SETM3_A9_RELEASE_M3P:
		m3_ctrl(dinfo, RELEASE, PRINTER);
		break;
	case M3IOC_SETM3_A9_RELEASE_M3S:
		m3_ctrl(dinfo, RELEASE, SCANNER);
		break;
	case M3IOC_SETM3_A9_RESET_M3P:
		m3_ctrl(dinfo, RESET, PRINTER);
		break;
	case M3IOC_SETM3_A9_RESET_M3S:
		m3_ctrl(dinfo, RESET, SCANNER);
		break;
	case M3IOC_RESET_AVISION_SUBSYS:
		reset_avision_subsys(dinfo);
		break;
	default:
		dev_info(dinfo->dev, "Unknown IOCTL\n");
		err = -EFAULT;
		break;
	}
	return err;
}

static const struct file_operations m3_smd_fops = {
	.owner = THIS_MODULE,
	.open = m3_smd_open,
	.release = m3_smd_close,
	.unlocked_ioctl = m3_smd_ioctl,
	.read = m3_smd_read,
	.write = m3_smd_write
};

static DEVICE_ATTR(m3_state, S_IRUGO | S_IWUSR, NULL, NULL);

static struct attribute *m3smd_attr[] = {
	&dev_attr_m3_state.attr,
	NULL
};

static struct attribute_group m3smd_attr_group = {
	.attrs = m3smd_attr
};

static int m3_ctrl(struct m3_smd_driver_info *info, int release, int printer/*1: printer, 0: scanner*/)
{
	u32 val;

	val = readl(m3ctrl);

	if(release) {
		if(printer) {
		    val |= A9_RELEASE_M3_PRINT; //enabled hclk and release mcu for print
		}
		else {
		    val |= A9_RELEASE_M3_SCAN; //enabled hclk and release mcu for scan
		}
		writel(val, m3ctrl);
	        //writel(0x1f, m3ctrl); //to release m3

	}
	else {
	        if(printer) {
		   val &= ~A9_RESET_M3_PRINT;
	        }
	        else {
		   val &= ~A9_RESET_M3_SCAN;
	        }
		writel(val, m3ctrl);
	        //writel(0x00, m3ctrl); //to reset m3
	}

	return 0;
}


static int reset_avision_subsys(struct m3_smd_driver_info *info)
{
	u32 val;

	// first, reset printer and scanner
	m3_ctrl(info, RESET, PRINTER);
	m3_ctrl(info, RESET, SCANNER);

	//second, clock gate for avision subsystem
	val = readl(m3ctrl + PRCM_BUS_PWR_CTRL);
	val &= ~PRCM_AVISION_PWR_STATE;
	writel(val, m3ctrl + PRCM_BUS_PWR_CTRL);
	while((readl(m3ctrl + PRCM_BUS_PWR_STATUS) & PRCM_AVISION_PWR_SLEEP) != 0x08);

	val = readl(m3ctrl + PRCM_BUS_PWR_CTRL);
	val |= PRCM_AVISION_PWR_STATE;
	writel(val, m3ctrl + PRCM_BUS_PWR_CTRL);
	while(((readl(m3ctrl + PRCM_BUS_PWR_STATUS) >> 3) & 0x1) != 0x0);


	return 0;

}

static int m3_smd_probe(struct platform_device *pdev)
{
	struct m3_smd_driver_info *dinfo;
	struct resource *res;
	int ret = 0, status = -EIO;
	dev_t dev_id;

	dev_err(&pdev->dev, "%s(): Enter\n", __func__);

	dinfo = kzalloc(sizeof(*dinfo), GFP_KERNEL);
	if (dinfo == NULL) {
		dev_err(&pdev->dev, "Could not alloc memory for dinfo.\n");
		return -ENOMEM;
	}
	dinfo->dev = &pdev->dev;

	//M3_S
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Could not get M3_S memory resource.\n");
		status = -ENODEV;
		goto fail1;
	}

	printk("size 0x%08x  address: 0x%08x\n", resource_size(res), res->start);
	dinfo->m3_s_smd_base = ioremap_nocache(res->start, resource_size(res));
	if (IS_ERR(dinfo->m3_s_smd_base)) {
		status = PTR_ERR(dinfo->m3_s_smd_base);
		goto fail1;
	}

	//M3_P
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "Could not get M3_P memory resource.\n");
		status = -ENODEV;
		goto fail1;
	}

	dinfo->m3_p_smd_base = ioremap_nocache(res->start, resource_size(res));
	if (IS_ERR(dinfo->m3_p_smd_base)) {
		status = PTR_ERR(dinfo->m3_p_smd_base);
		goto fail1;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&pdev->dev, "Could not get AP2M3 IRQ CTRL memory resource.\n");
		status = -ENODEV;
		goto fail1;
	}

    m3ctrl = ioremap(A9_RELEASE_M3_CTRL, 0x10);
	if(!m3ctrl) {
		dev_err(&pdev->dev, "%s(): Enter\n", __func__);
		status = -ENOMEM;
		goto fail1;
	}

	m3load = ioremap(SCM_BASE_M3, 0x10);
	if(!m3load) {
		dev_err(&pdev->dev, "%s(): Enter\n", __func__);
		status = -ENOMEM;
		goto fail1;
	}


#ifdef NUFRONT_PINCTRL_NO_IMPL
	//CPU_TO_M3_IRQ_CTRL = devm_ioremap_resource(&pdev->dev, res);
	CPU_TO_M3_IRQ_CTRL = ioremap_nocache(res->start, resource_size(res));
	if (IS_ERR(CPU_TO_M3_IRQ_CTRL)) {
		status = PTR_ERR(CPU_TO_M3_IRQ_CTRL);
		goto fail1;
	}
	CPU_TO_M3_IRQ_ENA = CPU_TO_M3_IRQ_CTRL + 0x4; //a9 scm register 384 "CPU_TO_MCU_IRQ_ENA"
	CPU_TO_M3_IRQ_EOI = CPU_TO_M3_IRQ_CTRL + 0x8; //a9 scm register 388 "CPU_TO_MCU_EOI"
#else
	CPU_TO_M3_IRQ_CTRL = CPU_TO_MCU_IRQ;
	CPU_TO_M3_IRQ_ENA = CPU_TO_MCU_IRQ_ENA;
	CPU_TO_M3_IRQ_EOI = CPU_TO_MCU_EOI;
#endif

	dinfo->m3s_2ap_irq1 = platform_get_irq(pdev, 0);
	dinfo->m3s_2ap_irq0 = platform_get_irq(pdev, 1);
	dinfo->m3p_2ap_irq1 = platform_get_irq(pdev, 2);
	dinfo->m3p_2ap_irq0 = platform_get_irq(pdev, 3);
	dinfo->m3p_wdt_irq = platform_get_irq(pdev, 4);
	dinfo->m3s_wdt_irq = platform_get_irq(pdev, 5);
	if (dinfo->m3s_2ap_irq0 == -ENXIO || dinfo->m3s_2ap_irq1 == -ENXIO
		|| dinfo->m3p_2ap_irq1 == -ENXIO || dinfo->m3p_2ap_irq1 == -ENXIO
		|| dinfo->m3s_wdt_irq == -ENXIO || dinfo->m3p_wdt_irq == -ENXIO) {

		dev_err(&pdev->dev, "%s: get irq failed\n", __func__);
		ret = -ENXIO;
		goto fail1;
	}



	spin_lock_init(&m3s_intr_lock);
	spin_lock_init(&m3p_intr_lock);

	ret = devm_request_irq(&pdev->dev, dinfo->m3s_2ap_irq0,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_HIGH, "m3smd", dinfo);
	if (ret) {
		dev_err(&pdev->dev, "%s: request m3s_2ap_irq0 failed\n", __func__);
		goto fail1;
	}

	ret = devm_request_irq(&pdev->dev, dinfo->m3s_2ap_irq1,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_HIGH, "m3smd", dinfo);
	if (ret) {
		dev_err(&pdev->dev, "%s: request m3s_2ap_irq1 failed\n", __func__);
		goto fail1;
	}

	ret = devm_request_irq(&pdev->dev, dinfo->m3p_2ap_irq0,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_HIGH, "m3smd", dinfo);
	if (ret) {
		dev_err(&pdev->dev, "%s: request m3p_2ap_irq0 failed\n", __func__);
		goto fail1;
	}

	ret = devm_request_irq(&pdev->dev, dinfo->m3p_2ap_irq1,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_HIGH, "m3smd", dinfo);
	if (ret) {
		dev_err(&pdev->dev, "%s: request m3p_2ap_irq1 failed\n", __func__);
		goto fail1;
	}

	//{for wdtp irq interrupt
	ret = devm_request_irq(&pdev->dev, dinfo->m3p_wdt_irq,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_RISING, "m3smd", dinfo);

	if (ret) {
		dev_err(&pdev->dev, "%s: request m3p_wdt_irq failed\n", __func__);
		goto fail1;
	}
	//}

	//{for wdts irq interrupt
	ret = devm_request_irq(&pdev->dev, dinfo->m3s_wdt_irq,
			m3_2ap_irq_handler,
			IRQF_TRIGGER_RISING, "m3smd", dinfo);

	if (ret) {
		dev_err(&pdev->dev, "%s: request m3s_wdt_irq failed\n", __func__);
		goto fail1;
	}
	//}


	/* create class, device and create attribure files*/
	dinfo->m3_smd_class = class_create(THIS_MODULE, "telink-m3smd");
	if (IS_ERR(dinfo->m3_smd_class)) {
		dev_err(&pdev->dev, "%s(): Error creating m3smd class\n", __func__);
		status = PTR_ERR(dinfo->m3_smd_class);
		goto fail1;
	}

	status = alloc_chrdev_region(&dev_id, 0, 1, "m3smd");
	if (status) {
		dev_err(&pdev->dev,"%s(): request device major failed\n", __func__);
		goto fail1;
	}

	modem_major = MAJOR(dev_id);

	cdev_init(&dinfo->cdev, &m3_smd_fops);

	dinfo->cdev.owner = THIS_MODULE;

	status = cdev_add(&dinfo->cdev, dev_id, 1);
	if (status) {
		dev_err(&pdev->dev, "%s(): Failed to add char device!\n", __func__);
		goto fail1;
	}

	dinfo->m3_smd = device_create(dinfo->m3_smd_class, &pdev->dev,
			MKDEV(modem_major, 0), NULL, "m3smd");
	if (IS_ERR(dinfo->m3_smd)) {
		dev_err(&pdev->dev, "%s(): Error creating m3-smd device.\n", __func__);
		status = PTR_ERR(dinfo->m3_smd);
		goto fail1;
	}

	status = sysfs_create_group(&dinfo->m3_smd->kobj, &m3smd_attr_group);
	if (status < 0) {
		dev_err(&pdev->dev,"%s(): sysfs_create_group failed.\n", __func__);
		goto fail1;
	}

	platform_set_drvdata(pdev,dinfo);

	dev_info(&pdev->dev,"%s(): DONE\n", __func__);

	return 0;

fail1:
	if (dinfo->m3_s_smd_base)
		iounmap(dinfo->m3_s_smd_base);
	if (dinfo->m3_p_smd_base)
		iounmap(dinfo->m3_p_smd_base);
	if (CPU_TO_M3_IRQ_CTRL) {
#ifdef NUFRONT_PINCTRL_NO_IMPL
		iounmap(CPU_TO_M3_IRQ_CTRL);
#endif
	}

	if(m3ctrl)
		iounmap(m3ctrl);
	if(m3load)
		iounmap(m3load);
	if (dinfo)
		kfree(dinfo);
	return status;
}

/*---------------------------------------------------------------------------*/
static int m3_smd_remove(struct platform_device *pdev)
{
	int irq_idx;
	int irq;
	struct m3_smd_driver_info *dinfo = platform_get_drvdata(pdev);

	device_destroy(dinfo->m3_smd_class, MKDEV(modem_major, 0));
	class_destroy(dinfo->m3_smd_class);

	for (irq_idx=0; irq_idx<2; irq_idx++)
	{
		irq = platform_get_irq(pdev, irq_idx);
		if (irq > 0) {
			free_irq(irq, dinfo);
		}
	}

	platform_set_drvdata(pdev,NULL);

	kfree(dinfo);

	iounmap(m3ctrl);
	pr_info("m3_smd_remove(): DONE\n");

	return 0;
}

static struct of_device_id m3_smd_dt_ids[] = {
	{ .compatible = "npsc,m3-smd", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, m3_smd_dt_ids);

static struct platform_driver m3_smd_driver = {
	.driver = {
		.name = "npsc,m3-smd",
		.owner = THIS_MODULE,
		.of_match_table = m3_smd_dt_ids,
	},
	.probe 		= m3_smd_probe,
	.remove 	= m3_smd_remove,
};
module_platform_driver(m3_smd_driver);

MODULE_DESCRIPTION("Nufront M3 Share Memory Driver");
MODULE_AUTHOR("Nufront.Nusmart.BSP");
MODULE_LICENSE("GPL");
