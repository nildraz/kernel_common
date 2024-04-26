#ifndef AVI_M3_CLIENT_H
#define AVI_M3_CLIENT_H

enum mcu_id {
    VM3_MCU0,
    VM3_MCU1,
    VM3_MCU_INVALID
};

enum mcu_signal {
    MCU_SIG0,
    MCU_SIG1,
    MCU_SIG_INVALID
};

struct device;

struct mcu_client {
    enum mcu_id id;
    const char *name;
    struct device *dev;
    irq_handler_t isr0;
    irq_handler_t isr1;
    irq_handler_t wdt;
    void *data;
};


#define CHAN_FIFO_SIZE   1024

#include <linux/kfifo.h>

struct channel {
    void * base;
    size_t length;
    wait_queue_head_t wait;
    DECLARE_KFIFO_PTR(fifo, unsigned char);
};


extern int vm3_mcu_client_register(struct mcu_client *);
extern void vm3_mcu_client_unregister(struct mcu_client *);
extern int vm3_mcu_device_signal(struct mcu_client *, enum mcu_signal);
extern void * vm3_mcu_get_resource(struct mcu_client *, size_t *);


struct mbox_msg
{
    u32 size;
    u8 data[];
};


#endif /* AVI_M3_CLIENT_H */
