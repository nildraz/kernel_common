/*
 * Base driver for ENE IO373X chip
 *
 * Copyright (C) 2010 ENE TECHNOLOGY INC.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include "nufront-dbg.h"

#define	 PRINT_DBG_INFO
#define ERR(fmt, args...) printk(KERN_EMERG "nufront_dbg(error):" fmt, ## args)
#define OUT(fmt, args...) printk(KERN_EMERG "nufront_dbg(data):" fmt, ## args)

#ifdef	PRINT_DBG_INFO
#define DBG_PRINT(fmt, args...) printk(KERN_INFO "nufront_dbg(debug):" fmt, ## args)
#else
#define DBG_PRINT(fmt, args...) /* not debugging: nothing */
#endif

#ifndef SZ_16K
#define SZ_16K	0x4000
#endif

//#define BUS_DBG
#ifdef BUS_DBG
#define NETWORK_CONTROL_OFFSET 0x20
#define L5_WK_LA  0x058c1000
#define L5_PER_LA 0x06c01000
#define L4_CFG_LA 0x05401000

#define TIMEOUT_BASE_SHIFT 0x8
#define TIMEOUT_BASE_MASK   ~(0x7 << TIMEOUT_BASE_SHIFT)
#define TIMEOUT_BASE_SET(x) ((x) << TIMEOUT_BASE_SHIFT)

#define FLAG_MASK_OFFSET 0x100
/* l3 */
#define L3_REG_SI_FLAG_STATUS (0x07207000 + 0x110)
#define L5_PER_SFLAG0_BIT  0
#define L4_CFG_SFLAG0_BIT  1

/* l4 */
#define L4_CFG_LA_FLAG_STATUS (0x05401000 + 0x110)
#define L5_PER_LA_FLAG_STATUS (0x06C01000 + 0x110)

/* l5 */
#define L5_WK_LA_FLAG_STATUS (0x058C1000 + 0x110)

/* graphic bus */
#define TA_GRAPHIC_CONTROL  (0x07211000 + 0x20)

/* l3m */
#define TA_MEM0_CONTROL     (0x08001000 + 0x20)

/* l3 */
#define TA_PER_CPU_CONTROL   (0x07205000 + 0x20)
#define TA_PER_OTHER_CONTROL (0x07205400 + 0x20)
#define TA_L4_CFG_CONTROL 	 (0x07205800 + 0x20)
#define TA_RAMMEM_CONTROL 	 (0x07206000 + 0x20)
#define TA_L3M_CONTROL    	 (0x07206400 + 0x20)
#define TA_ROM_CONTROL    	 (0x07206800 + 0x20)

/* L5_PER */
#define TA_I2S_CONTROL 	  	(0x06c03800 + 0x20)
#define TA_UART0_CONTROL 	(0x06c04000 + 0x20)
#define TA_UART1_CONTROL 	(0x06c04400 + 0x20)
#define TA_UART2_CONTROL 	(0x06c04800 + 0x20)
#define TA_UART3_CONTROL 	(0x06c04c00 + 0x20)
#define TA_GPIO1_CONTROL 	(0x06c05c00 + 0x20)
#define TA_I2C0_CONTROL 	(0x06c06000 + 0x20)
#define TA_I2C1_CONTROL 	(0x06c06400 + 0x20)
#define TA_I2C2_CONTROL 	(0x06c06800 + 0x20)
#define TA_I2C3_CONTROL 	(0x06c06c00 + 0x20)
#define TA_TIME1_CONTROL 	(0x06c07000 + 0x20)
#define TA_SPI3_CONTROL 	(0x06c07400 + 0x20)
#define TA_PWM_CONTROL 		(0x06c07800 + 0x20)
#define TA_EFUSE_CONTROL 	(0x06c07c00 + 0x20)

/* L4_CFG */
#define TA_CS_CONTROL 		(0x05403000 + 0x20)
#define TA_SD0_CONTROL 		(0x05403400 + 0x20)
#define TA_DMA330_CONTROL 	(0x05403800 + 0x20)
#define TA_DMA330_S_CONTROL	(0x05403c00 + 0x20)
#define TA_MALI_CONTROL		(0x05404400 + 0x20)
#define TA_DISP0_CONTROL		(0x05404800 + 0x20)
#define TA_DISP1_CONTROL		(0x05404c00 + 0x20)
#define TA_PCTL0_CONTROL		(0x05405000 + 0x20)
#define TA_USBOHCI_CONTROL		(0x05405c00 + 0x20)
#define TA_USBEHCI_CONTROL		(0x05406000 + 0x20)
#define TA_USBOTG_CONTROL		(0x05406400 + 0x20)
#define TA_DEC_CONTROL		(0x05406800 + 0x20)
#define TA_ENC_CONTROL		(0x05406c00 + 0x20)
#define TA_TWOD_CONTROL		(0x05407000 + 0x20)
#define TA_WAKEUP_CONTROL	(0x05407400 + 0x20)
#define TA_CAMERIC_CONTROL	(0x05407c00 + 0x20)
#define TA_SD1_CONTROL	(0x05408800 + 0x20)
#define TA_SD2_CONTROL	(0x05408c00 + 0x20)
#define TA_DSI_CONTROL	(0x05409000 + 0x20)
#define TA_CSI_CONTROL	(0x05409400 + 0x20)

#define REQ_TIMEOUT_SHIFT 0x8
#define REQ_TIMEOUT_MASK   ~(0x7 << REQ_TIMEOUT_SHIFT)
#define REQ_TIMEOUT_SET(x) ((x) << REQ_TIMEOUT_SHIFT)

/* l5_WAKEUP */
#define TA_GPIO_WK_CONTROL	0x058c1800 + 0x20
#define TA_PRCM_CONTROL	0x058c1c00 + 0x20
#define TA_SCM_CONTROL	0x058c2000 + 0x20
#define TA_TIMER0_CONTROL	0x058c2400 + 0x20
#define TA_SARRAM_CONTROL	0x058c2800 + 0x20

struct bus_table {
	unsigned int index;
	unsigned int la_base;
	unsigned int la_va;
};

static struct bus_table nu7t_bus_table[] = {
	{ 0, L5_WK_LA},
	{ 1, L5_PER_LA},
	{ 2, L4_CFG_LA}
};

struct bit_map {
	unsigned int index;
	unsigned bit;
	char *source_name;
};

struct error_source_table {
	unsigned int addr;
	unsigned int bit_map_sum;
	struct bit_map *source_bit_map;
};

/* locate the error source of L3 */
static struct bit_map l3_bit_maps[] = {
	{ .index = 1, .bit = 0, .source_name = "L5_PER SFlag0" },
	{ .index = 2, .bit = 1, .source_name = "L4_GFG SFlag0" }
};

static struct error_source_table l3_error_source = {
	.addr = L3_REG_SI_FLAG_STATUS,
	.bit_map_sum = 2,
	.source_bit_map = l3_bit_maps,
};

struct bus_table agent_table[] = {
#if 0
	/* graphic bus */
	{0, TA_GRAPHIC_CONTROL },
	/* l3m */
	{1, TA_MEM0_CONTROL  },
	/* l3 */
	{2, TA_PER_CPU_CONTROL },
	{3, TA_PER_OTHER_CONTROL },
	{4, TA_L4_CFG_CONTROL },
	{5, TA_RAMMEM_CONTROL },
	{6, TA_L3M_CONTROL },
	{7, TA_ROM_CONTROL },
#endif
	/* L5_PER */
	{8, TA_I2S_CONTROL },
	{9, TA_UART0_CONTROL },
	{10, TA_UART1_CONTROL },
	{11, TA_UART2_CONTROL },
	{12, TA_UART3_CONTROL },
	{13, TA_GPIO1_CONTROL },
	{14, TA_I2C0_CONTROL },
	{0, TA_I2C1_CONTROL },
	{0, TA_I2C2_CONTROL },
	{0, TA_I2C3_CONTROL },
	{0, TA_TIME1_CONTROL },
	{0, TA_SPI3_CONTROL },
	{0, TA_PWM_CONTROL },
	{0, TA_EFUSE_CONTROL },
	/* L4_CFG */
	{0, TA_CS_CONTROL },
	{0, TA_SD0_CONTROL },
	{0, TA_DMA330_CONTROL },
	{0, TA_DMA330_S_CONTROL },
	{0, TA_MALI_CONTROL },
	{0, TA_DISP0_CONTROL },
	{0, TA_DISP1_CONTROL },
	{0, TA_PCTL0_CONTROL },
	{0, TA_USBOHCI_CONTROL },
	{0, TA_USBEHCI_CONTROL },
	{0, TA_USBOTG_CONTROL },
	{0, TA_DEC_CONTROL },
	{0, TA_ENC_CONTROL },
	{0, TA_TWOD_CONTROL	},
	{0, TA_WAKEUP_CONTROL },
	{0, TA_CAMERIC_CONTROL },
	{0, TA_SD1_CONTROL },
	{0, TA_SD2_CONTROL },
	{0, TA_DSI_CONTROL },
	{0, TA_CSI_CONTROL }
};

static irqreturn_t bus_error_handler(int this_irq, void *dev_id)
{
	unsigned int value, i;

	value = readl((void *)l3_error_source.addr);

	printk(KERN_ERR "[BUS ERROR]: error source: 0x%08x, value: 0x%08x\n", l3_error_source.addr, value);

	for (i = 0; i < l3_error_source.bit_map_sum; i++)
		if ((value >> l3_error_source.source_bit_map[i].bit) & 0x1)
			printk(KERN_ERR "[BUS Error]: Error source: %s\n", l3_error_source.source_bit_map[i].source_name);

	BUG_ON(1);
	return IRQ_HANDLED;
}

static void bus_time_out_init(void * la_base)
{
	unsigned int value;
	value = readl((la_base + NETWORK_CONTROL_OFFSET));
	writel((value & TIMEOUT_BASE_MASK) | TIMEOUT_BASE_SET(4), la_base + NETWORK_CONTROL_OFFSET);
}

static void individual_time_out_mask_init(unsigned int la_base, unsigned int range)
{
	unsigned int value, i;

	value = readl((void *)la_base + FLAG_MASK_OFFSET);

	for (i = 0; i < range; i++)
		value |= (1 << i);

	writel(value, (void *)la_base + FLAG_MASK_OFFSET);
}

static void time_out_mask_init(void)
{
	individual_time_out_mask_init(nu7t_bus_table[0].la_va, 2);
	individual_time_out_mask_init(nu7t_bus_table[1].la_va, 15);
	individual_time_out_mask_init(nu7t_bus_table[2].la_va, 10);
}

static void agent_time_out_init(void *agent_ctrol)
{
	unsigned int value;
	value = readl(agent_ctrol);
	writel((value & REQ_TIMEOUT_MASK) | REQ_TIMEOUT_SET(4), agent_ctrol);
}

static int bus_error_report_init(void)
{
	int i, ret;
	void __iomem *busio;

	/* Program the base time out interval and enable */
	for(i = 0; i < ARRAY_SIZE(nu7t_bus_table); i++) {
		busio = ioremap(nu7t_bus_table[i].la_base, SZ_1K);
		if (!busio) {
			ERR("remap addr failed!\n");
			return -EFAULT;
		}
		nu7t_bus_table[i].la_va = (unsigned int)busio;
		bus_time_out_init(busio);
	}

	/* Program the individual time out interval and enable */
	for(i = 0; i < ARRAY_SIZE(agent_table); i++) {
		busio = ioremap(agent_table[i].la_base, SZ_1K);
		if (!busio) {
			ERR("remap addr failed!\n");
			return -EFAULT;
		}
		agent_time_out_init(busio);
	}

	/* Program the individual time out mask */
	time_out_mask_init();

	busio = ioremap(l3_error_source.addr, SZ_1K);
	if (!busio) {
		ERR("remap addr failed!\n");
		return -EFAULT;
	}
	l3_error_source.addr = (unsigned int)busio;

	/* bus error interrupt num is 235 */
	ret = request_irq(235, bus_error_handler, 0, "TL778X-BUS", NULL);
	if (ret != 0) {
		dev_err(NULL, "Failed to request BUS IRQ %d: %d\n",
				235, ret);
		return -EINTR;
	}

	return 0;
}
#endif /* BUS_DBG */

struct nufront_dbg {
	struct class *nufront_dbg_class;
	struct kobject *kobj;
	struct cdev cdev;
	dev_t devt;
};
struct nufront_dbg *g_dbg;
struct dbg_cmd g_syscmd;

static char cmd_name[7][10] = {
	"read", "write", "bic", "bis", "memset", "memdisplay"};

static int check_cmd(struct dbg_cmd *cmd)
{
	int err = 0;
	if ((cmd->cmd >= D_MAX) || (cmd->cmd < 0)) {
		ERR("invalid command!(%d)\n", cmd->cmd);
		err = 1;
	}
	if ((cmd->addr > 0xc0000000) || (cmd->addr%4 != 0)) {
		ERR("invalid address!(0x%08x)\n", cmd->addr);
		err = 1;
	}

	if (cmd->cmd == D_MEMSET || cmd->cmd == D_DISPLAY) {
		if ((cmd->len <= 0) || (cmd->len > SZ_16K)) {
			ERR("invalid len!(%d)\n", cmd->len);
			err = 1;
		}
	}

	if (err)
		return -EFAULT;

	return 0;
}
static int show_data(void __iomem *addr, unsigned int paddr, int len)
{
	int idx = 0;
	for (idx = 0; idx < len; idx += 4) {
		OUT("0x%08x-0x%08x 0x%08x 0x%08x 0x%08x\n", paddr + idx*4, \
				(*((unsigned int *)(addr + idx*4))), \
				(*((unsigned int *)(addr + idx*4 + 4))),
				(*((unsigned int *)(addr + idx*4 + 8))), \
				(*((unsigned int *)(addr + idx*4 + 0xc))));
	}
	return 0;
};
static int mem_set(void __iomem *addr, int len, unsigned int value)
{
	int idx = 0;
	for (idx = 0; idx < len*4; idx += 4) {
		*((unsigned int *)(addr + idx)) = value;
	}
	return 0;
}

int process_cmd(struct dbg_cmd *cmd)
{
	void __iomem *mmio;
	unsigned int value;

	if (check_cmd(cmd) < 0)
		return -EFAULT;

	mmio = ioremap(cmd->addr, SZ_16K);
	if (!mmio) {
		ERR("remap addr failed!\n");
		return -EFAULT;
	}

	switch (cmd->cmd) {
	case D_READ:
		cmd->value = *((unsigned int *)mmio);
		break;
	case D_WRITE:
		*((unsigned int *)mmio) = cmd->value;
		break;
	case D_BIC:
		value = *((unsigned int *)mmio);
		value &= ~cmd->value;
		*((unsigned int *)mmio) = value;
		cmd->value = *((unsigned int *)mmio);
		break;
	case D_BIS:
		value = *((unsigned int *)mmio);
		value |= cmd->value;
		*((unsigned int *)mmio) = value;
		cmd->value = *((unsigned int *)mmio);
		break;
	case D_MEMSET:
		mem_set(mmio, cmd->len, cmd->value);
		break;
	case D_DISPLAY:
		show_data(mmio, cmd->addr, cmd->len);
		break;
	default:
		break;
	};
	iounmap(mmio);
	return 0;
}

int get_value(const char *buf, ssize_t len, unsigned long *result)
{
	if ((len > 2) && (buf[0] == '0') && (buf[1] == 'x' || buf[1] == 'X')) {
		return strict_strtoul(buf, 16, result);
	} else {
		return strict_strtoul(buf, 10, result);
	};
	return -EINVAL;
}

static int ioctl_dbg_cmd(struct nufront_dbg *nufront_dbg, unsigned long arg)
{
	struct dbg_cmd cmd;

	if (copy_from_user(&cmd, (void __user *) arg, sizeof(struct dbg_cmd))) {
		ERR("copy from user failed!\n");
		return -EFAULT; /* some data not copied.*/
	}

	process_cmd(&cmd);

	if (copy_to_user((void __user *) arg, &cmd, sizeof(struct dbg_cmd))) {
		ERR("copy to user failed!\n");
		return -EFAULT;
	}

	return 0;
}

static ssize_t cmd_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int idx = 0;
	int offset = 0;
	for (idx = 0; idx < D_MAX; idx++) {
		if (idx == g_syscmd.cmd)
			offset += sprintf(buf + offset, "(%s)\t", cmd_name[idx]);
		else
			offset += sprintf(buf + offset, "%s\t", cmd_name[idx]);
	}
	sprintf(buf+offset, "\n");
	return strlen(buf);
}

static ssize_t cmd_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int idx = 0, valid = 0;
	char *end = memchr(buf, '\n', count);
	int len = end ? end-buf : count;

	for (idx = 0; idx < D_MAX; idx++) {
		if (!strncmp(buf, cmd_name[idx], len)) {
			g_syscmd.cmd = idx;
			valid = 1;
		}
	}
	if (!valid)
		ERR("invalid cmd :%s\n", buf);
	return count;
};

static ssize_t value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "value:0x%08x\t(%u)\n", g_syscmd.value, g_syscmd.value);
}

static ssize_t value_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long ul = 0;
	if (!get_value(buf, count, &ul)) {
		g_syscmd.value = ul;
	} else
		ERR("value store failed.\n");
	return count;
};

static ssize_t addr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "addr:0x%08x\t(%u)\n", g_syscmd.addr, g_syscmd.addr);
}

static ssize_t addr_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long ul = 0;
	if (!get_value(buf, count, &ul)) {
		g_syscmd.addr = ul;
	} else
		ERR("addr store failed.\n");
	return count;
};

static ssize_t len_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "len:0x%08x\t(%u)\n", g_syscmd.len, g_syscmd.len);
}

static ssize_t len_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long ul = 0;
	if (!get_value(buf, count, &ul)) {
		g_syscmd.len = ul;
	} else
		ERR("len store failed.\n");
	return count;
};

static ssize_t result_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int ret = process_cmd(&g_syscmd);
	return sprintf(buf, "cmd %s -- value:0x%08x\t(%u)\n", ret == 0 ? "ok" : "failed", \
			g_syscmd.value, g_syscmd.value);
}

static DEVICE_ATTR(cmd, (S_IRUGO|S_IWUSR), cmd_show, cmd_store);
static DEVICE_ATTR(value, (S_IRUGO|S_IWUSR), value_show, value_store);
static DEVICE_ATTR(addr, (S_IRUGO|S_IWUSR), addr_show, addr_store);
static DEVICE_ATTR(len, (S_IRUGO|S_IWUSR), len_show, len_store);
static DEVICE_ATTR(result, S_IRUGO, result_show, NULL);

int nufront_init_sysfs(struct nufront_dbg *nufront_dbg)
{
	struct kobject *obj = kobject_create_and_add("nufront-debug", NULL);
	int ret = 0;

	if (!obj) {
		ERR("create sysfs object failed.\n");
		goto obj_err;
	}
	ret = sysfs_create_file(obj, &dev_attr_cmd.attr);
	if (ret) {
		ERR("create sysfs cmd file failed.\n");
		goto cmd_err;
	}
	ret = sysfs_create_file(obj, &dev_attr_value.attr);
	if (ret) {
		ERR("create sysfs value file failed.\n");
		goto value_err;
	}
	ret = sysfs_create_file(obj, &dev_attr_addr.attr);
	if (ret) {
		ERR("create sysfs addr file failed.\n");
		goto addr_err;
	}
	ret = sysfs_create_file(obj, &dev_attr_len.attr);
	if (ret) {
		ERR("create sysfs len file failed.\n");
		goto len_err;
	}
	ret = sysfs_create_file(obj, &dev_attr_result.attr);
	if (ret) {
		ERR("create sysfs result file failed.\n");
		goto result_err;
	}
	nufront_dbg->kobj = obj;
	return 0;

result_err:
	sysfs_remove_file(obj, &dev_attr_len.attr);
len_err:
	sysfs_remove_file(obj, &dev_attr_addr.attr);
addr_err:
	sysfs_remove_file(obj, &dev_attr_value.attr);
value_err:
	sysfs_remove_file(obj, &dev_attr_cmd.attr);
cmd_err:
	kobject_del(obj);
obj_err:
	return ret;
}

void nufront_remove_sysfs(struct nufront_dbg *nufront_dbg)
{
	struct kobject *obj = nufront_dbg->kobj;
	if (obj) {
		sysfs_remove_file(obj, &dev_attr_cmd.attr);
		sysfs_remove_file(obj, &dev_attr_value.attr);
		sysfs_remove_file(obj, &dev_attr_addr.attr);
		sysfs_remove_file(obj, &dev_attr_len.attr);
		sysfs_remove_file(obj, &dev_attr_result.attr);
		kobject_del(obj);
		nufront_dbg->kobj = NULL;
	}
}

static long nufront_dbg_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct nufront_dbg *nufront_dbg = filp->private_data;
	int ret = 0;

	if (_IOC_TYPE(cmd) != NUFRONT_DBG_MAGIC) {
		DBG_PRINT("Not nufront_DBG_MAGIC\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case NUFRONT_DBG_CMD:
		ret = ioctl_dbg_cmd(nufront_dbg, arg);
		break;
	default:
		DBG_PRINT("Unsupported ioctl\n");
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static int nufront_dbg_open(struct inode *inode, struct file *filp)
{
	struct nufront_dbg *nufront_dbg = container_of(inode->i_cdev, struct nufront_dbg, cdev);
	DBG_PRINT("nufront_dbg_open()\n");
	filp->private_data = nufront_dbg;

	return 0;
}

static int nufront_dbg_release(struct inode *inode, struct file *filp)
{
	struct nufront_dbg *nufront_dbg = container_of(inode->i_cdev, struct nufront_dbg, cdev);
	DBG_PRINT("nufront_dbg_release()\n");
	filp->private_data = nufront_dbg;

	return 0;
}

static const struct file_operations nufront_dbg_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= nufront_dbg_ioctl,
	.open		= nufront_dbg_open,
	.release	= nufront_dbg_release,
};

static int nufront_dbg_create_cdev_node(struct nufront_dbg *nufront_dbg)
{
	int status;
	dev_t devt;
	struct device *dev;
	struct class *nufront_dbg_class;
	bool is_class_created = false, is_region_allocated = false, is_cdev_added = false, is_device_created = false;

	DBG_PRINT("nufront_dbg_create_cdev_node .. \n");

	/* Create class */
	nufront_dbg_class = class_create(THIS_MODULE, "nufront-dbg");
	status = IS_ERR(nufront_dbg_class) ? PTR_ERR(nufront_dbg_class) : 0;
	if (status < 0) {
		DBG_PRINT("class_create() failed -- %d\n", status);
		goto error_exit;
	}
	is_class_created = true;

	/* Alloc chrdev region. */
	status = alloc_chrdev_region(&devt, 0, 1, "nufront-dbg");
	if (status < 0) {
		DBG_PRINT("alloc_chrdev_region() failed -- %d\n", status);
		goto error_exit;
	}
	is_region_allocated = true;

	/* Add cdev.*/
	cdev_init(&nufront_dbg->cdev, &nufront_dbg_fops);
	status = cdev_add(&nufront_dbg->cdev, devt, 1);
	if (status < 0) {
		DBG_PRINT("cdev_add() failed -- %d\n", status);
		goto error_exit;
	}
	is_cdev_added = true;

	/* Create device */
	dev = device_create
		(
		 nufront_dbg_class,
		 NULL,			/* parent device (struct device *) */
		 devt,
		 nufront_dbg,		/* caller's context */
		 "nufront-dbg"
		);
	status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
	if (status < 0) {
		DBG_PRINT("device_create() failed -- %d\n", status);
		goto error_exit;
	}
	is_device_created = true;

	/* Succeed.*/
	nufront_dbg->nufront_dbg_class = nufront_dbg_class;
	nufront_dbg->devt = devt;
	return 0;

error_exit:

	if (is_device_created)
		device_destroy(nufront_dbg_class, devt);
	if (is_cdev_added)
		cdev_del(&nufront_dbg->cdev);
	if (is_region_allocated)
		unregister_chrdev_region(devt, 1);
	if (is_class_created)
		class_destroy(nufront_dbg_class);

	return status;
}

/*
 * Undo nufront_dbg_create_cdev_node().
 * Call this only if the char device node was ever created successfully.
 */
static void nufront_dbg_destroy_cdev_node(struct nufront_dbg *nufront_dbg)
{
	device_destroy(nufront_dbg->nufront_dbg_class, nufront_dbg->devt);
	cdev_del(&nufront_dbg->cdev);
	unregister_chrdev_region(nufront_dbg->devt, 1);
	class_destroy(nufront_dbg->nufront_dbg_class);
}

struct nufront_dbg *nufront_dbg_probe(void)
{
	struct nufront_dbg *nufront_dbg = NULL;
	bool cdev_node_created = false;
	int err = 0;

#ifdef BUS_DBG
	bus_error_report_init();
#endif

	nufront_dbg = kzalloc(sizeof(*nufront_dbg), GFP_KERNEL);
	if (!nufront_dbg)
		return ERR_PTR(-ENOMEM);

	err = nufront_dbg_create_cdev_node(nufront_dbg);
	if (!err)
		cdev_node_created = true;
	else
		goto error_exit;
	return nufront_dbg;

error_exit:

	if (cdev_node_created)
		nufront_dbg_destroy_cdev_node(nufront_dbg);

	kfree(nufront_dbg);

	return ERR_PTR(err);
}

int nufront_dbg_remove(struct nufront_dbg *nufront_dbg)
{
	DBG_PRINT("nufront_dbg_remove\n");
	nufront_dbg_destroy_cdev_node(nufront_dbg);

	kfree(nufront_dbg);

	return 0;
}
static int __init nufront_dbg_init(void)
{
	g_dbg = nufront_dbg_probe();
	if (!g_dbg)
		return -EINVAL;
	return nufront_init_sysfs(g_dbg);
}

static void __exit nufront_dbg_exit(void)
{
	if (g_dbg) {
		nufront_remove_sysfs(g_dbg);
		nufront_dbg_remove(g_dbg);
		g_dbg = NULL;
	}
}

module_init(nufront_dbg_init);
module_exit(nufront_dbg_exit);

MODULE_AUTHOR("zeyuan");
MODULE_DESCRIPTION("nufront dbg driver");
MODULE_LICENSE("GPL");

