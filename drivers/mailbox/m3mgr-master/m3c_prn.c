#include <linux/module.h>
#include <linux/interrupt.h>
//#include <linux/slab.h>         /* kmalloc/kfree */
#include <linux/fs.h>
#include <linux/miscdevice.h>
//#include <asm/uaccess.h>
//#include <linux/sched.h>
//#include <linux/wait.h>         /* wait queue */

#include "m3client.h"
#include "m3c_common.h"

static struct m3c_options options = {
    .fifo_size = CHAN_FIFO_SIZE,
};
module_param_named(fifosize, options.fifo_size, int, S_IRUGO);
MODULE_PARM_DESC(fifosize, "FIFO size");


static const struct file_operations m3p_fops = {
    .owner   = THIS_MODULE,
    .open    = m3c_open,
    .release = m3c_close,
    .read    = m3c_read,
    .write   = m3c_write,
    .poll    = m3c_poll,
};


static struct m3c_mbox_driver driver = {
    .client = {
        .id = VM3_MCU1,
        .name = "m3_prn",
        .isr0 = m3c_irq0_handler,
        .isr1 = m3c_irq1_handler,
        .wdt  = m3c_wdt_handler,
    },
    .device = {
        .minor = MISC_DYNAMIC_MINOR,
        .name  = "m3_prn_mbox",
        .fops  = &m3p_fops,
    },
    .msg_sent = ATOMIC_INIT(0),
};


static int __init m3p_prn_init(void)
{
    return m3c_mailbox_driver_init(&driver, &options);
}
module_init(m3p_prn_init);


static void __exit m3p_prn_exit(void)
{
    m3c_mailbox_driver_exit(&driver);
}
module_exit(m3p_prn_exit);



MODULE_AUTHOR("William Jeng <william_jeng@avision.com.tw>");
MODULE_LICENSE("Dual BSD/GPL");
