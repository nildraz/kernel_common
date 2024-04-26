#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>         /* kmalloc/kfree */
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/wait.h>         /* wait queue */

#include "m3client.h"

struct demo_driver {
    struct mcu_client client;
    struct miscdevice device;
    struct channel w;
    struct channel r;
    #if 0
    struct semaphore open_lock;
    #endif
    struct work_struct work;
    struct workqueue_struct * workqueue;
};

//#define CLIENT2DRV(c) container_of(c, struct demo_driver, client)
#define CLIENT2DRV(c) ((void *)c)

static irqreturn_t m3c_irq0_handler(int irq, void *data)
{
    struct mcu_client *client = data;
    struct demo_driver *driver = CLIENT2DRV(client);
    struct channel *chan = &driver->r;
    int *len = chan->base;

    pr_debug("%s: got irq %d, driver: %p, %d bytes\n", __func__, irq, driver, *len);

    pr_debug("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&chan->fifo), kfifo_avail(&chan->fifo), kfifo_size(&chan->fifo));
    kfifo_in(&chan->fifo, chan->base, *len);
    pr_info("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&chan->fifo), kfifo_avail(&chan->fifo), kfifo_size(&chan->fifo));

    /* wakeup */
    wake_up_interruptible(&chan->wait);
    return IRQ_HANDLED;
}

static irqreturn_t m3c_irq1_handler(int irq, void *data)
{
    pr_info("%s: got irq %d, data: %p\n", __func__, irq, data);
    return IRQ_HANDLED;
}

static irqreturn_t m3c_wdt_handler(int irq, void *data)
{
    pr_info("%s: got irq %d, data: %p\n", __func__, irq, data);
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
    struct demo_driver *drv = container_of(file->private_data, struct demo_driver, device);
    int ret;
    unsigned int copied = 0;

    pr_info("%s: need %d bytes, off: %llu\n", __func__, len, *ppos);

    if (kfifo_is_empty(&drv->r.fifo) /*|| kfifo_len(&drv->r.fifo) < len*/) {
        if (file->f_flags & O_NONBLOCK) {
            pr_debug("%s: empty, nonblocking\n", __func__);
            return -EAGAIN;
        }
        pr_debug("%s: empty, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
        pr_info("%s: empty, waiting for incomming data\n", __func__);
        if (wait_event_interruptible(drv->r.wait, (!kfifo_is_empty(&drv->r.fifo)))) {
            pr_debug("%s: empty, abort waiting\n", __func__);
            return -ERESTARTSYS;
        }
    }

    pr_info("%s: got incomming data, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
    ret = kfifo_to_user(&drv->r.fifo, buf, len, &copied);
    if (ret) {
        pr_debug("%s: copy data from fifo failed, %d\n", __func__, ret);
        return ret;
    }
    pr_notice("%s: copied %d bytes, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            copied, kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
    return copied;
}

static ssize_t m3c_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *ppos)
{
    struct demo_driver *drv = container_of(file->private_data, struct demo_driver, device);
    int ret;
    unsigned int copied = 0;

    pr_info("%s: write %d bytes, off=%lld/%lld\n", __func__, len, *ppos, file->f_pos);

    if (kfifo_is_full(&drv->w.fifo) || kfifo_avail(&drv->w.fifo) < len) {
        if (file->f_flags & O_NONBLOCK) {
            pr_debug("%s: full, nonblocking\n", __func__);
            return -EAGAIN;
        }
        pr_debug("%s: full, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
        pr_info("%s: full, waiting for consuming data\n", __func__);
        if (wait_event_interruptible(drv->w.wait, (!(kfifo_is_full(&drv->w.fifo) || kfifo_avail(&drv->w.fifo) < len)))) {
            return -ERESTARTSYS;
        }
    }
    pr_info("%s: ready to write data, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
    ret = kfifo_from_user(&drv->w.fifo, buf, len, &copied);
    if (ret) {
        pr_debug("%s: copy data from fifo failed, %d\n", __func__, ret);
        return ret;
    }
    pr_notice("%s: copied %d bytes, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            copied, kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
    /* schedule a send work */
    queue_work(drv->workqueue, (struct work_struct *)&drv->work);

    return copied;
}

static void m3c_async_send(struct work_struct *work)
{
    struct demo_driver *drv = container_of(work, struct demo_driver, work);
    int size;

    pr_info("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
    if (kfifo_out_peek(&drv->w.fifo, (unsigned char *)&size, sizeof size)) {
        if (0 < size && size <= kfifo_len(&drv->w.fifo)) {
            int n = kfifo_out(&drv->w.fifo, drv->w.base, size);
            pr_notice("%s: size of first record in fifo: %d, get %d out\n", __func__, size, n);
            /* send signal */
            vm3_mcu_device_signal(&drv->client, MCU_SIG0);
        } else {
            pr_info("%s: invalid data length: %d\n", __func__, size);
            if (!kfifo_is_empty(&drv->w.fifo)) {
                kfifo_reset_out(&drv->w.fifo);
                pr_debug("%s: flush all data\n", __func__);
            }
        }
    }
    /* wakeup if someone is still waiting for writing */
    wake_up_interruptible(&drv->w.wait);
    if (kfifo_is_empty(&drv->w.fifo)) {
        pr_info("%s: finished\n", __func__);
        return;
    }
    pr_debug("%s: fifo remain %d bytes, add next work\n", __func__, kfifo_len(&drv->w.fifo));
    schedule();
    queue_work(drv->workqueue, work);
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
        .name = "m3c_demo_client",
        .isr0 = m3c_irq0_handler,
        .isr1 = m3c_irq1_handler,
        .wdt  = m3c_wdt_handler,
        .data = &driver.client,
    },
    .device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "m3c_demo",
        .fops  = &m3c_fops,
    },
};


static int __init m3c_demo_init(void)
{
    int err = misc_register(&driver.device);
    if (err) return err;
    driver.client.dev = driver.device.this_device;
    err = vm3_mcu_client_register(&driver.client);
    if (err) {
        driver.client.dev = NULL;
        misc_deregister(&driver.device);
        goto failed;
    }
    /* channel */
    driver.w.base = vm3_mcu_get_resource(&driver.client, &driver.w.length);
    driver.w.length /= 2;
    driver.r = driver.w;
    driver.r.base += driver.w.length;
    err = kfifo_alloc(&driver.w.fifo, CHAN_FIFO_SIZE, GFP_KERNEL);
    if (!err) {
        err = kfifo_alloc(&driver.r.fifo, CHAN_FIFO_SIZE, GFP_KERNEL);
        if (err) {
            kfifo_free(&driver.w.fifo);
            goto failed;
        }
    }
    init_waitqueue_head(&driver.w.wait);
    init_waitqueue_head(&driver.r.wait);
    /* setup async work */
    INIT_WORK((struct work_struct *)&driver.work, m3c_async_send);
    driver.workqueue = create_singlethread_workqueue("mcu_demo_wq");
    if (IS_ERR(driver.workqueue)) {
        pr_err("%s: create workqueue failed\n", __func__);
        err = PTR_ERR(driver.workqueue);
        goto failed;
    }
    /* done */
    pr_debug("%s: driver: %p, mem: r(%p+0x%x) w(%p+0x%x)\n", __func__, &driver,
            driver.w.base, driver.w.length, driver.r.base, driver.r.length);
    return 0;
failed:
    pr_err("%s: failed: %d\n", __func__, err);
    return err;
}
module_init(m3c_demo_init);


static void __exit m3c_demo_exit(void)
{
    pr_debug("%s\n", __func__);
    vm3_mcu_client_unregister(&driver.client);
    driver.client.dev = NULL;
    misc_deregister(&driver.device);
    kfifo_free(&driver.w.fifo);
    kfifo_free(&driver.r.fifo);
    flush_workqueue(driver.workqueue);
    destroy_workqueue(driver.workqueue);
}
module_exit(m3c_demo_exit);



MODULE_AUTHOR("William Jeng <william_jeng@avision.com.tw>");
MODULE_LICENSE("Dual BSD/GPL");
