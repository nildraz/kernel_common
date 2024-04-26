#include <linux/module.h>
//#include <linux/init.h>
//#include <linux/kernel.h>
//#include <asm/io.h>             /* for ioremap_nocache */
#include <linux/io.h>           /* for devm_ioremap_nocache */
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/firmware.h>
#include <linux/limits.h>

#include "m3client.h"


struct mcu_device {
    void __iomem *base;
    size_t length;
    unsigned int irq0;
    unsigned int irq1;
    unsigned int wdt;
    unsigned int irq0_eoi_mask;
    unsigned int irq1_eoi_mask;
    //unsigned int wdt_eoi_mask;
    unsigned int reset_mask;
    unsigned int bootaddr_reg;
    unsigned int loadaddr;
    char filename[NAME_MAX];
    struct mcu_client *client;
#ifdef CONFIG_SYSFS
    struct kobject *kobj;
    struct kobj_attribute attr_reset;
    struct kobj_attribute attr_bootaddr;
    struct kobj_attribute attr_loadaddr;
    struct kobj_attribute attr_filename;
    struct kobj_attribute attr_reload;
#endif
};

struct mcu_driver {
    struct platform_device *pdev;
    struct mcu_device mcu[2];
};

static spinlock_t intr_lock;

//pinctrl for nufront
// for prcm at 0x05200000 + 0x210
extern int nufront_prcm_write(u32 val, u32 reg);
extern int nufront_prcm_read(u32 *val, u32 reg);
// for scm at 0x05210000  + {0x380, 0x384, 0x388}
extern int nufront_scm_write(u32 val, u32 reg);
extern int nufront_scm_read(u32 *val, u32 reg);


#define PRCM_M3_CTRL  0x210

#define PRCM_MCU0_RST_MASK  (1<<1)
#define PRCM_MCU1_RST_MASK  (1<<0)


#define MCU0_BOOTADDR_REG   0x1F4
#define MCU1_BOOTADDR_REG   0x1F0


#define CPU_TO_MCU_IRQ 0x380
#define CPU_TO_MCU_IRQ_ENA 0x384
#define CPU_TO_MCU_EOI 0x388

#define MCU0_IRQ0_ENA_MASK  (1<<2)
#define MCU0_IRQ1_ENA_MASK  (1<<3)
#define MCU1_IRQ0_ENA_MASK  (1<<0)
#define MCU1_IRQ1_ENA_MASK  (1<<1)

#define MCU0_IRQ0_IRQ_MASK  (1<<2)
#define MCU0_IRQ1_IRQ_MASK  (1<<3)
#define MCU1_IRQ0_IRQ_MASK  (1<<0)
#define MCU1_IRQ1_IRQ_MASK  (1<<1)

#define MCU0_IRQ0_EOI_MASK  (1<<2)
#define MCU0_IRQ1_EOI_MASK  (1<<3)
#define MCU1_IRQ0_EOI_MASK  (1<<0)
#define MCU1_IRQ1_EOI_MASK  (1<<1)

static irqreturn_t vm3_mcu_irq_handler(int irq, void *data)
{
    irqreturn_t ret = IRQ_HANDLED;
    struct mcu_device *mcu = data;
    unsigned long flags;

    if (!mcu || !mcu->client) return ret;
    pr_debug("%s: got irq %d in {%d %d %d} from mcu%d\n", __func__,
            irq, mcu->irq0, mcu->irq1, mcu->wdt, mcu->client->id);
    if (irq == mcu->irq0) {
        if (mcu->client->isr0)
            ret = mcu->client->isr0(irq, mcu->client);
        // set eoi to clear interrupt state
        spin_lock_irqsave(&intr_lock, flags);
        nufront_scm_write(mcu->irq0_eoi_mask, CPU_TO_MCU_EOI);
        spin_unlock_irqrestore(&intr_lock, flags);
    }
    else if (irq == mcu->irq1) {
        if (mcu->client->isr1)
            ret = mcu->client->isr1(irq, mcu->client);
        // set eoi to clear interrupt state
        spin_lock_irqsave(&intr_lock, flags);
        nufront_scm_write(mcu->irq1_eoi_mask, CPU_TO_MCU_EOI);
        spin_unlock_irqrestore(&intr_lock, flags);
    }
    else if (irq == mcu->wdt) {
        if (mcu->client->wdt)
            ret = mcu->client->wdt(irq, mcu->client);
        /* restart mcu */
    }
    return ret;
}


static void vm3_mcu_del(struct mcu_device *mcu, void *data)
{
    u32 val;
    pr_debug("%s: del mcu%d\n", __func__, mcu->client->id);

#ifdef CONFIG_SYSFS
    sysfs_remove_link(firmware_kobj, mcu->client->name);
#endif

#if 0
    if (mcu->irq0) devm_free_irq(mcu->client->dev, mcu->irq0, data);
    if (mcu->irq1) devm_free_irq(mcu->client->dev, mcu->irq1, data);
    if (mcu->wdt)  devm_free_irq(mcu->client->dev, mcu->wdt, data);
    if (mcu->base) devm_iounmap(mcu->client->dev, mcu->base);
//#else
//    devres_release_all(mcu->client->dev);
#endif
    mcu->irq0 = mcu->irq1 = mcu->wdt = 0;
    mcu->base = NULL;

    nufront_scm_read(&val, CPU_TO_MCU_IRQ_ENA);
    nufront_scm_write(val & ~(mcu->irq0_eoi_mask | mcu->irq1_eoi_mask), CPU_TO_MCU_IRQ_ENA);
    nufront_scm_write((mcu->irq0_eoi_mask | mcu->irq1_eoi_mask), CPU_TO_MCU_EOI);
}

static int vm3_mcu_add(struct device *dev, struct mcu_device *mcu, const char *name,
                       struct resource *res, unsigned int irq0, unsigned int irq1, unsigned int wdt,
                       irq_handler_t handler, void *data)
{
    int ret = 0;
    u32 val;

    if (!res || irq0 == -ENXIO || irq1 == -ENXIO || wdt == -ENXIO) return -ENXIO;

    ret = devm_request_irq(dev, irq0, handler, IRQF_TRIGGER_HIGH, name, data);
    if (ret) {
        return ret;
    }
    ret = devm_request_irq(dev, irq1, handler, IRQF_TRIGGER_HIGH, name, data);
    if (ret) {
        return ret;
    }
    ret = devm_request_irq(dev, wdt, handler, IRQF_TRIGGER_RISING, name, data);
    if (ret) {
        return ret;
    }

    mcu->length = resource_size(res);
    mcu->base = devm_ioremap_nocache(dev, res->start, mcu->length);
    if (IS_ERR(mcu->base)) {
        ret = PTR_ERR(mcu->base);
        mcu->length = 0;
        mcu->base = NULL;
        return ret;
    }

    mcu->irq0 = irq0;
    mcu->irq1 = irq1;
    mcu->wdt = wdt;

    nufront_scm_write((mcu->irq0_eoi_mask | mcu->irq1_eoi_mask), CPU_TO_MCU_EOI);
    nufront_scm_read(&val, CPU_TO_MCU_IRQ_ENA);
    nufront_scm_write(val | (mcu->irq0_eoi_mask | mcu->irq1_eoi_mask), CPU_TO_MCU_IRQ_ENA);

#ifdef CONFIG_SYSFS
    ret = sysfs_create_link(firmware_kobj, mcu->kobj, name);
#endif

    pr_debug("%s: add mcu%d, mem: 0x%08x -> %p (%d) irq: %d %d %d\n", __func__,
            mcu->client->id, res->start, mcu->base, mcu->length, irq0, irq1, wdt);
    return 0;
}


#ifdef CONFIG_SYSFS

static ssize_t reset_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_reset);
    u32 val;
    nufront_prcm_read(&val, PRCM_M3_CTRL);
    return sprintf(buf, "%d\n", !(val & mcu->reset_mask));
}

static ssize_t reset_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_reset);
    u32 val;
    bool reset;
    if (strtobool(buf, &reset) < 0)
        return -EINVAL;
    /* TODO: need lock */
    nufront_prcm_read(&val, PRCM_M3_CTRL);
    if (reset == (!(val & mcu->reset_mask)))
        return count;
    if (reset)
        val &= ~mcu->reset_mask;
    else
        val |= mcu->reset_mask;
    nufront_prcm_write(val, PRCM_M3_CTRL);
    return count;
}

static ssize_t bootaddr_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_bootaddr);
    u32 addr;
    nufront_scm_read(&addr, mcu->bootaddr_reg);
    return sprintf(buf, "0x%08X\n", addr);
}

static ssize_t bootaddr_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_bootaddr);
    char *end;
    u32 addr = simple_strtoul(buf, &end, 0);
    if (end == buf)
        return -EINVAL;
    nufront_scm_write(addr, mcu->bootaddr_reg);
    return count;
}

static ssize_t loadaddr_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_loadaddr);
    return sprintf(buf, "0x%08X\n", mcu->loadaddr);
}

static ssize_t loadaddr_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_loadaddr);
    char *end;
    u32 addr = simple_strtoul(buf, &end, 0);
    if (end == buf)
        return -EINVAL;
    mcu->loadaddr = addr;
    return count;
}

static ssize_t filename_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_filename);
    return snprintf(buf, PAGE_SIZE, "%s\n", mcu->filename);
}

static ssize_t filename_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_filename);
    char *p;
    strncpy(mcu->filename, buf, count);
    p = strchr(mcu->filename, '\n');
    if (p)
        *p = '\0';
    return count;
}

static ssize_t reload_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct mcu_device *mcu = container_of(attr, struct mcu_device, attr_reload);

    if (mcu->filename[0] && mcu->loadaddr) {
        const struct firmware *f;
        pr_debug("reloading '%s' at 0x%x...\n", mcu->filename, mcu->loadaddr);
        if (request_firmware(&f, mcu->filename, NULL) == 0) {
            size_t len = (f->size + PAGE_SIZE) & ~(PAGE_SIZE - 1);
            struct resource *res = request_mem_region(mcu->loadaddr, len, "firmware");
            if (res) {
                char *p = ioremap(mcu->loadaddr, len);
                if (p) {
                    pr_debug("load firmware: %p size %u/%u\n", f->data, f->size, len);
                    memcpy(p, f->data, f->size);
                    iounmap(p);
                }
                release_mem_region(mcu->loadaddr, len);
            }
            release_firmware(f);
        } else
            pr_err("load firmware failed\n");
    }
    return count;
}

#endif  /* CONFIG_SYSFS */

static struct mcu_driver drv_ = {
    .mcu = {
        [0] = {
            .reset_mask = PRCM_MCU0_RST_MASK,
            .bootaddr_reg = MCU0_BOOTADDR_REG,
            .loadaddr = 0x0,
#ifdef CONFIG_SYSFS
            .attr_reset = __ATTR(reset, 0660, reset_show, reset_store),
            .attr_bootaddr = __ATTR(bootaddr, 0660, bootaddr_show, bootaddr_store),
            .attr_loadaddr = __ATTR(loadaddr, 0660, loadaddr_show, loadaddr_store),
            .attr_filename = __ATTR(filename, 0660, filename_show, filename_store),
            .attr_reload   = __ATTR(reload, 0220, NULL, reload_store),
#endif
        },
        [1] = {
            .reset_mask = PRCM_MCU1_RST_MASK,
            .bootaddr_reg = MCU1_BOOTADDR_REG,
            .loadaddr = 0x0,
#ifdef CONFIG_SYSFS
            .attr_reset = __ATTR(reset, 0660, reset_show, reset_store),
            .attr_bootaddr = __ATTR(bootaddr, 0660, bootaddr_show, bootaddr_store),
            .attr_loadaddr = __ATTR(loadaddr, 0660, loadaddr_show, loadaddr_store),
            .attr_filename = __ATTR(filename, 0660, filename_show, filename_store),
            .attr_reload   = __ATTR(reload, 0220, NULL, reload_store),
#endif
        },
    },
};


static int vm3_mcu_probe(struct platform_device *pdev)
{
    int err;
    pr_debug("%s\n", __func__);

    drv_.pdev = pdev;
    spin_lock_init(&intr_lock);

#ifdef CONFIG_SYSFS
    drv_.mcu[0].kobj = kobject_create_and_add("mcu0", firmware_kobj);
    err = sysfs_create_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_reset.attr);
    err = sysfs_create_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_bootaddr.attr);
    err = sysfs_create_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_loadaddr.attr);
    err = sysfs_create_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_filename.attr);
    err = sysfs_create_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_reload.attr);
    drv_.mcu[1].kobj = kobject_create_and_add("mcu1", firmware_kobj);
    err = sysfs_create_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_reset.attr);
    err = sysfs_create_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_bootaddr.attr);
    err = sysfs_create_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_loadaddr.attr);
    err = sysfs_create_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_filename.attr);
    err = sysfs_create_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_reload.attr);
#endif

    platform_set_drvdata(pdev, &drv_);
    return 0;
}

static int vm3_mcu_remove(struct platform_device *pdev)
{
    platform_set_drvdata(pdev, NULL);

#ifdef CONFIG_SYSFS
    sysfs_remove_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_reset.attr);
    sysfs_remove_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_bootaddr.attr);
    sysfs_remove_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_loadaddr.attr);
    sysfs_remove_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_filename.attr);
    sysfs_remove_file(drv_.mcu[0].kobj, &drv_.mcu[0].attr_reload.attr);
    kobject_put(drv_.mcu[0].kobj);
    sysfs_remove_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_reset.attr);
    sysfs_remove_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_bootaddr.attr);
    sysfs_remove_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_loadaddr.attr);
    sysfs_remove_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_filename.attr);
    sysfs_remove_file(drv_.mcu[1].kobj, &drv_.mcu[1].attr_reload.attr);
    kobject_put(drv_.mcu[1].kobj);
#endif

    pr_debug("%s\n", __func__);
    return 0;
}

static struct of_device_id vm3_mcu_match[] = {
	{ .compatible = "npsc,m3-smd", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, vm3_mcu_match);

static struct platform_driver vm3_mcu_driver = {
	.probe 		= vm3_mcu_probe,
	.remove 	= vm3_mcu_remove,
	.driver = {
		.name = "npsc,m3-smd",
		.owner = THIS_MODULE,
		.of_match_table = vm3_mcu_match,
	},
};
module_platform_driver(vm3_mcu_driver);



int vm3_mcu_client_register(struct mcu_client *client)
{
    struct platform_device *pdev;
    if (NULL == client) return -EINVAL;

    pdev = drv_.pdev;

    switch (client->id) {
    case VM3_MCU0:
        pr_debug("%s: register mcu%d client\n", __func__, client->id);
        drv_.mcu[0].irq0_eoi_mask = MCU0_IRQ0_EOI_MASK;
        drv_.mcu[0].irq1_eoi_mask = MCU0_IRQ1_EOI_MASK;
        drv_.mcu[0].client = client;
        return vm3_mcu_add(client->dev, drv_.mcu, client->name,
                           platform_get_resource(pdev, IORESOURCE_MEM, 0),
                           platform_get_irq(pdev, 1),  /* 38 m3s_2ap_irq0 = 70 */
                           platform_get_irq(pdev, 0),  /* 37 m3s_2ap_irq1 = 69 */
                           platform_get_irq(pdev, 5),  /* 35 m3s_wdt_irq  = 67 */
                           vm3_mcu_irq_handler, drv_.mcu);
    case VM3_MCU1:
        pr_debug("%s: register mcu%d client\n", __func__, client->id);
        drv_.mcu[1].irq0_eoi_mask = MCU1_IRQ0_EOI_MASK;
        drv_.mcu[1].irq1_eoi_mask = MCU1_IRQ1_EOI_MASK;
        drv_.mcu[1].client = client;
        return vm3_mcu_add(client->dev, drv_.mcu + 1, client->name,
                           platform_get_resource(pdev, IORESOURCE_MEM, 1),
                           platform_get_irq(pdev, 3),  /* 40 m3p_2ap_irq0 = 72 */
                           platform_get_irq(pdev, 2),  /* 39 m3p_2ap_irq1 = 71 */
                           platform_get_irq(pdev, 4),  /* 36 m3p_wdt_irq  = 68 */
                           vm3_mcu_irq_handler, drv_.mcu + 1);
    default:
        break;
    }
    return -EINVAL;
}
EXPORT_SYMBOL(vm3_mcu_client_register);


void vm3_mcu_client_unregister(struct mcu_client *client)
{
    if (NULL == client) return;
    switch (client->id) {
    case VM3_MCU0:
        if (client == drv_.mcu[0].client) {
            vm3_mcu_del(drv_.mcu, drv_.mcu);
            drv_.mcu[0].client = NULL;
            pr_debug("%s: unregister mcu%d client\n", __func__, client->id);
            return;
        }
        break;
    case VM3_MCU1:
        if (client == drv_.mcu[1].client) {
            vm3_mcu_del(drv_.mcu + 1, drv_.mcu + 1);
            drv_.mcu[1].client = NULL;
            pr_debug("%s: unregister mcu%d client\n", __func__, client->id);
            return;
        }
        break;
    default:
        break;
    }
}
EXPORT_SYMBOL(vm3_mcu_client_unregister);


int vm3_mcu_device_signal(struct mcu_client *client, enum mcu_signal sig)
{
    struct mcu_device *mcu;
    unsigned long flags;

    switch (client->id) {
    case VM3_MCU0:
        mcu = drv_.mcu;
        break;
    case VM3_MCU1:
        mcu = drv_.mcu + 1;
        break;
    default:
        return -ENODEV;
    }
    pr_debug("%s: send signal %d to mcu%d\n", __func__, sig, client->id);
    switch (sig) {
    case MCU_SIG0:
        spin_lock_irqsave(&intr_lock, flags);
        nufront_scm_write(mcu->irq0_eoi_mask, CPU_TO_MCU_IRQ);
        spin_unlock_irqrestore(&intr_lock, flags);
        break;
    case MCU_SIG1:
        spin_lock_irqsave(&intr_lock, flags);
        nufront_scm_write(mcu->irq1_eoi_mask, CPU_TO_MCU_IRQ);
        spin_unlock_irqrestore(&intr_lock, flags);
        break;
    default:
        pr_err("%s: unknown signal: %d\n", __func__, sig);
        break;
    }
    return 0;
}
EXPORT_SYMBOL(vm3_mcu_device_signal);


void * vm3_mcu_get_resource(struct mcu_client *client, size_t *length)
{
    struct mcu_device *mcu;

    switch (client->id) {
    case VM3_MCU0:
        mcu = drv_.mcu;
        break;
    case VM3_MCU1:
        mcu = drv_.mcu + 1;
        break;
    default:
        return NULL;
    }
    if (length) *length = mcu->length;
    return mcu->base;
}
EXPORT_SYMBOL(vm3_mcu_get_resource);


MODULE_AUTHOR("William Jeng <william_jeng@avision.com.tw>");
MODULE_LICENSE("Dual BSD/GPL");
