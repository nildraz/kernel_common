//#include <linux/module.h>
#include <linux/interrupt.h>
//#include <linux/slab.h>         /* kmalloc/kfree */
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/sched.h>
#include <linux/wait.h>         /* wait queue */

#include "m3client.h"
#include "m3c_common.h"


static int m3c_recv(struct m3c_mbox_driver *drv)
{
    struct channel *chan = &drv->r;
    struct mbox_msg *msg = chan->base;

    if (msg->size > kfifo_avail(&chan->fifo)) {
        return -ENOSPC;
    }

    /* ready to fetch new message */
    pr_debug("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&chan->fifo), kfifo_avail(&chan->fifo), kfifo_size(&chan->fifo));
    kfifo_in(&chan->fifo, chan->base, msg->size);
    pr_debug("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&chan->fifo), kfifo_avail(&chan->fifo), kfifo_size(&chan->fifo));
    /* send signal */
    vm3_mcu_device_signal(&drv->client, MCU_SIG1);
    /* wakeup */
    wake_up_interruptible(&chan->wait);
    return msg->size;
}

irqreturn_t m3c_irq0_handler(int irq, void *data)
{
    struct mcu_client *client = data;
    struct m3c_mbox_driver *drv = CLIENT2DRV(client);

    pr_debug("%s: got irq %d, data: %p\n", __func__, irq, data);
    atomic_dec(&drv->msg_sent);
    /* m3 got the message, ready to send the next message */
    queue_work(drv->workqueue, (struct work_struct *)&drv->work);
    return IRQ_HANDLED;
}

static void m3c_delayed_recv(struct work_struct *work)
{
    struct m3c_mbox_driver *drv = container_of(work, struct m3c_mbox_driver, delayed_recv.work);
    struct channel *chan = &drv->r;
    struct mbox_msg *msg = chan->base;

    pr_debug("%s: driver: %p, %d bytes, available space %d\n",
             __func__, drv, msg->size, kfifo_avail(&chan->fifo));
    if (0 > m3c_recv(drv)) {
        queue_delayed_work(drv->workqueue, &drv->delayed_recv, msecs_to_jiffies(5));
    }
}

irqreturn_t m3c_irq1_handler(int irq, void *data)
{
    struct mcu_client *client = data;
    struct m3c_mbox_driver *drv = CLIENT2DRV(client);
    struct channel *chan = &drv->r;
    struct mbox_msg *msg = chan->base;

    pr_debug("%s: got irq %d, driver: %p, %d bytes, available space %d\n",
             __func__, irq, drv, msg->size, kfifo_avail(&chan->fifo));
    if (0 > m3c_recv(drv)) {
        queue_delayed_work(drv->workqueue, &drv->delayed_recv, msecs_to_jiffies(5));
    }
    return IRQ_HANDLED;
}

irqreturn_t m3c_wdt_handler(int irq, void *data)
{
    pr_debug("%s: got irq %d, data: %p\n", __func__, irq, data);
    return IRQ_HANDLED;
}

int m3c_open(struct inode *inode, struct file *file)
{
    struct miscdevice *dev = file->private_data;
    struct m3c_mbox_driver *drv = container_of(dev, struct m3c_mbox_driver, device);
    pr_debug("%s: driver: %p\n", __func__, drv);
    return 0;
}

int m3c_close(struct inode *inode, struct file *file)
{
    pr_debug("%s\n", __func__);
    return 0;
}

ssize_t m3c_read(struct file *file, char __user *buf,
                        size_t len, loff_t *ppos)
{
    struct m3c_mbox_driver *drv = container_of(file->private_data, struct m3c_mbox_driver, device);
    int ret;
    unsigned int copied = 0;

    pr_debug("%s: need %d bytes, off: %llu\n", __func__, len, *ppos);

    if (kfifo_is_empty(&drv->r.fifo) /*|| kfifo_len(&drv->r.fifo) < len*/) {
        if (file->f_flags & O_NONBLOCK) {
            pr_debug("%s: empty, nonblocking\n", __func__);
            return -EAGAIN;
        }
        pr_debug("%s: empty, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
        pr_debug("%s: empty, waiting for incoming data\n", __func__);
        if (wait_event_interruptible(drv->r.wait, (!kfifo_is_empty(&drv->r.fifo)))) {
            pr_warning("%s: empty, abort waiting\n", __func__);
            return -ERESTARTSYS;
        }
    }

    pr_debug("%s: got incoming data, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
    ret = kfifo_to_user(&drv->r.fifo, buf, len, &copied);
    if (ret) {
        pr_err("%s: copy data from fifo failed, %d\n", __func__, ret);
        return ret;
    }
    pr_debug("%s: copied %d bytes, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            copied, kfifo_len(&drv->r.fifo), kfifo_avail(&drv->r.fifo), kfifo_size(&drv->r.fifo));
    return copied;
}

ssize_t m3c_write(struct file *file, const char __user *buf,
                         size_t len, loff_t *ppos)
{
    struct m3c_mbox_driver *drv = container_of(file->private_data, struct m3c_mbox_driver, device);
    int ret;
    unsigned int copied = 0;
    struct mbox_msg *msg = (void *)buf;

    pr_debug("%s: write %d bytes, off=%lld/%lld\n", __func__, len, *ppos, file->f_pos);

    /* integrity check */
    if (msg->size > len) {
        return -EINVAL;
    }
    if (msg->size > kfifo_size(&drv->w.fifo)) {
        return -EFBIG;
    }
    if (msg->size > kfifo_avail(&drv->w.fifo)) {
        if (file->f_flags & O_NONBLOCK) {
            pr_debug("%s: full, nonblocking\n", __func__);
            return -EAGAIN;
        }
        pr_debug("%s: full, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
        pr_debug("%s: full, waiting for consuming queued data\n", __func__);
        if (wait_event_interruptible(drv->w.wait, (!(msg->size > kfifo_avail(&drv->w.fifo))))) {
            pr_debug("%s: full, abort waiting for consuming queued data\n", __func__);
            return -ERESTARTSYS;
        }
    }
    pr_debug("%s: ready to write data, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
    if (atomic_sub_and_test(0, &drv->msg_sent) && 0 == kfifo_len(&drv->w.fifo)) {
        /* send the message directly */
        atomic_inc(&drv->msg_sent);
        //
        ret = copy_from_user(drv->w.base, buf, msg->size);
        if (ret) {
            pr_err("%s: copy data to fifo failed, %d\n", __func__, ret);
            return -EIO;
        }
        copied = msg->size;
        len -= copied;
        buf += copied;
        pr_debug("%s: copied %d bytes (directly), fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                copied, kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
        /* send signal */
        vm3_mcu_device_signal(&drv->client, MCU_SIG0);
    }
    if (len) {
        int rest_copied = 0;
        do {
            struct mbox_msg *msg = (void *)buf;
            if (msg->size > len)
                break;
            if (msg->size > kfifo_avail(&drv->w.fifo))
                break;
            ret = kfifo_from_user(&drv->w.fifo, buf, msg->size, &rest_copied);
            if (ret) {
                pr_err("%s: copy data to fifo failed, %d\n", __func__, ret);
                return (copied) ? copied : ret;
            }
            copied += rest_copied;
            len -= rest_copied;
            buf += rest_copied;
            pr_debug("%s: keep %d bytes, fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
                     rest_copied, kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
        } while (len);
        /* schedule a send work */
        queue_work(drv->workqueue, (struct work_struct *)&drv->work);
    }
    return copied;
}

static void m3c_async_send(struct work_struct *work)
{
    struct m3c_mbox_driver *drv = container_of(work, struct m3c_mbox_driver, work);
    int size, n;

    if (!kfifo_out_peek(&drv->w.fifo, (unsigned char *)&size, sizeof size))
        return;
    pr_debug("%s: fifo: used: %4d/unused: %4d/capacity: %d\n", __func__,
            kfifo_len(&drv->w.fifo), kfifo_avail(&drv->w.fifo), kfifo_size(&drv->w.fifo));
    if (size < 0 || size > kfifo_len(&drv->w.fifo)) {
        pr_warning("%s: invalid data length: %d\n", __func__, size);
        if (!kfifo_is_empty(&drv->w.fifo)) {
            kfifo_reset_out(&drv->w.fifo);
            pr_err("%s: flush all data\n", __func__);
        }
        return;
    }
    if (!atomic_sub_and_test(0, &drv->msg_sent))
        return;
    /* ready to send msg to mcu */
    atomic_inc(&drv->msg_sent);
    n = kfifo_out(&drv->w.fifo, drv->w.base, size);
    pr_debug("%s: size of first record in fifo: %d, get %d out\n", __func__, size, n);
    /* send signal */
    vm3_mcu_device_signal(&drv->client, MCU_SIG0);
    /* wakeup if someone is still waiting for writing */
    wake_up_interruptible(&drv->w.wait);
}

unsigned int m3c_poll(struct file *file, struct poll_table_struct *wait)
{
    struct m3c_mbox_driver *drv = container_of(file->private_data, struct m3c_mbox_driver, device);
    int status = 0;

    poll_wait(file, &drv->r.wait, wait);
    poll_wait(file, &drv->w.wait, wait);

    if(likely(kfifo_avail(&drv->w.fifo) >= 4))
        status |= POLLOUT | POLLWRNORM;

    if(unlikely(kfifo_len(&drv->r.fifo)))
        status |= POLLIN | POLLRDNORM;

    pr_debug("%s: %s, status: 0x%x (r 0x%x, w 0x%x)\n", __func__, drv->client.name, status,
             POLLIN | POLLRDNORM, POLLOUT | POLLWRNORM);

    return status;
}


int __init m3c_mailbox_driver_init(struct m3c_mbox_driver *drv, struct m3c_options *opts)
{
    int err = misc_register(&drv->device);
    if (err) return err;
    drv->client.dev = drv->device.this_device;
    err = vm3_mcu_client_register(&drv->client);
    if (err) {
        drv->client.dev = NULL;
        misc_deregister(&drv->device);
        goto failed;
    }
    /* setup channel */
    drv->w.base = vm3_mcu_get_resource(&drv->client, &drv->w.length);
    drv->w.length /= 2;
    drv->r = drv->w;
    drv->r.base += drv->w.length;
    if (opts->fifo_size < CHAN_FIFO_SIZE)
        opts->fifo_size = CHAN_FIFO_SIZE;
    else if (opts->fifo_size > drv->w.length) {
        opts->fifo_size = (drv->w.length + (CHAN_FIFO_SIZE / 2)) & ~(CHAN_FIFO_SIZE - 1);
    } else {
        opts->fifo_size = (opts->fifo_size + (CHAN_FIFO_SIZE / 2)) & ~(CHAN_FIFO_SIZE - 1);
    }
    err = kfifo_alloc(&drv->w.fifo, opts->fifo_size, GFP_KERNEL);
    if (!err) {
        err = kfifo_alloc(&drv->r.fifo, opts->fifo_size, GFP_KERNEL);
        if (err) {
            kfifo_free(&drv->w.fifo);
            goto failed;
        }
    }
    init_waitqueue_head(&drv->w.wait);
    init_waitqueue_head(&drv->r.wait);
    /* setup async work */
    INIT_WORK((struct work_struct *)&drv->work, m3c_async_send);
    INIT_DELAYED_WORK(&drv->delayed_recv, m3c_delayed_recv);
    drv->workqueue = create_singlethread_workqueue("mcu_demo_wq");
    if (IS_ERR(drv->workqueue)) {
        pr_err("%s: create workqueue failed\n", __func__);
        err = PTR_ERR(drv->workqueue);
        goto failed;
    }
    /* done */
    pr_debug("%s: drv: %p, mem: r(%p+0x%x) w(%p+0x%x)\n", __func__, drv,
            drv->w.base, drv->w.length, drv->r.base, drv->r.length);
    return 0;
failed:
    pr_err("%s: failed: %d\n", __func__, err);
    return err;
}

void __exit m3c_mailbox_driver_exit(struct m3c_mbox_driver *drv)
{
    pr_debug("%s: %s\n", __func__, drv->client.name);
    vm3_mcu_client_unregister(&drv->client);
    drv->client.dev = NULL;
    misc_deregister(&drv->device);
    kfifo_free(&drv->w.fifo);
    kfifo_free(&drv->r.fifo);
    flush_work(&drv->work);
    /*
     * flush_work(&drv->delayed_recv);
     */
    cancel_delayed_work_sync(&drv->delayed_recv);
    flush_workqueue(drv->workqueue);
    destroy_workqueue(drv->workqueue);
}
