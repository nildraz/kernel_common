#ifndef AVI_M3C_COMMON_H
#define AVI_M3C_COMMON_H


struct m3c_mbox_driver {
    struct mcu_client client;
    struct miscdevice device;
    struct channel w;
    struct channel r;
    struct work_struct work;
    struct delayed_work delayed_recv;
    struct workqueue_struct * workqueue;
    atomic_t msg_sent;
};

//#define CLIENT2DRV(c) container_of(c, struct m3c_mbox_driver, client)
#define CLIENT2DRV(c) ((void *)c)

extern irqreturn_t m3c_irq0_handler(int irq, void *data);
extern irqreturn_t m3c_irq1_handler(int irq, void *data);
extern irqreturn_t m3c_wdt_handler(int irq, void *data);

extern int m3c_open(struct inode *inode, struct file *file);
extern int m3c_close(struct inode *inode, struct file *file);
extern ssize_t m3c_read(struct file *file, char __user *buf, size_t len, loff_t *ppos);
extern ssize_t m3c_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos);
unsigned int m3c_poll(struct file *file, struct poll_table_struct *wait);


struct m3c_options
{
    int fifo_size;
};

extern int m3c_mailbox_driver_init(struct m3c_mbox_driver *drv, struct m3c_options *opts);
extern void m3c_mailbox_driver_exit(struct m3c_mbox_driver *drv);


#endif /* AVI_M3C_COMMON_H */
