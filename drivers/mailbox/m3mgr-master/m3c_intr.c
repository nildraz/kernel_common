#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>

#include "m3client.h"

struct demo_driver {
    struct mcu_client client;
    struct miscdevice device;
};


static irqreturn_t m3c_irq0_handler(int irq, void *data)
{
    pr_info("%s: got irq %d\n", __func__, irq);
    return IRQ_HANDLED;
}

static irqreturn_t m3c_irq1_handler(int irq, void *data)
{
    pr_info("%s: got irq %d\n", __func__, irq);
    return IRQ_HANDLED;
}

static irqreturn_t m3c_wdt_handler(int irq, void *data)
{
    pr_info("%s: got irq %d\n", __func__, irq);
    return IRQ_HANDLED;
}


static int m3c_open(struct inode *inode, struct file *file)
{
    struct miscdevice *dev = file->private_data;
    struct demo_driver *drv = container_of(dev, struct demo_driver, device);

    pr_debug("%s: driver: %p\n", __func__, drv);
    return 0;
}

static int m3c_close(struct inode *inode, struct file *file)
{
    pr_debug("%s\n", __func__);
    return 0;
}

static ssize_t m3c_read(struct file *file, char __user *buf,
                        size_t len, loff_t *ppos)
{
    pr_debug("%s: need %d bytes\n", __func__, len);
    return 0;
}

static ssize_t m3c_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *ppos)
{
    struct demo_driver *drv = container_of(file->private_data, struct demo_driver, device);
    struct mcu_client *client = &drv->client;
    char cmd[4];
    unsigned long remains = copy_from_user(cmd, buf, (len < sizeof(cmd)) ? len : sizeof(cmd));
    pr_info("%s: got %d/%lu bytes, %d\n", __func__, len, remains, cmd[0]);
    switch (cmd[0]) {
    case '0':
        vm3_mcu_device_signal(client, MCU_SIG0);
        break;
    case '1':
        vm3_mcu_device_signal(client, MCU_SIG1);
        break;
    default:
        pr_warning("%s: unknown cmd\n", __func__);
        break;
    }
    //vm3_mcu_client_write(client, addr, data, len);
    return len;
}

static const struct file_operations m3c_fops = {
    .owner   = THIS_MODULE,
    .open    = m3c_open,
    .release = m3c_close,
    .read    = m3c_read,
    .write   = m3c_write,
};


static struct demo_driver driver = {
    .client = {
        .id = VM3_MCU0,
        .name = "m3c_intr_client",
        .isr0 = m3c_irq0_handler,
        .isr1 = m3c_irq1_handler,
        .wdt  = m3c_wdt_handler,
    },
    .device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "m3c_intr",
        .fops  = &m3c_fops,
    },
};


static int __init m3c_intr_init(void)
{
    int err = misc_register(&driver.device);
    if (err) return err;
    driver.client.dev = driver.device.this_device;
    err = vm3_mcu_client_register(&driver.client);
    if (err) {
        driver.client.dev = NULL;
        misc_deregister(&driver.device);
        pr_err("%s: failed: %d\n", __func__, err);
        return err;
    }
    pr_debug("%s: driver: %p\n", __func__, &driver);
    return 0;
}
module_init(m3c_intr_init);


static void __exit m3c_intr_exit(void)
{
    pr_debug("%s\n", __func__);
    vm3_mcu_client_unregister(&driver.client);
    driver.client.dev = NULL;
    misc_deregister(&driver.device);
}
module_exit(m3c_intr_exit);



MODULE_AUTHOR("William Jeng <william_jeng@avision.com.tw>");
MODULE_LICENSE("Dual BSD/GPL");
