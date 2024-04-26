#ifndef DMALLOC_H
#define DMALLOC_H


struct dmb_handle {
    size_t size;
    unsigned int offset;
    void * rec;                 /* for internal use, don't modify it */
};


#define DEVICE_NAME "dma-demo"
#define DMALLOC_DEVPATH   "/dev/" DEVICE_NAME


#endif  /* DMALLOC_H */
