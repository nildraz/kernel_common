#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/init.h>

#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>

#include "dmalloc.h"



static int dma_demo_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int dma_demo_close(struct inode *inode, struct file *filp)
{
    return 0;
}

static int dma_demo_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct miscdevice *dev = filp->private_data;
    dma_addr_t dma_src = vma->vm_pgoff << PAGE_SHIFT;
    void * src = dma_to_virt(dev->this_device, dma_src);
    size_t size = vma->vm_end - vma->vm_start;

    vma->vm_flags |= VM_IO;
    pr_info("%s: cpu virt: %p, offset 0x%lx, size %d\n", __func__, src, vma->vm_pgoff << PAGE_SHIFT, size);

    if (filp->f_flags & O_SYNC)
        vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
    //vma->vm_page_prot = phys_mem_access_prot(filp, vma->vm_pgoff, size, vma->vm_page_prot);

    if (remap_pfn_range(vma,
                        vma->vm_start,
                        vma->vm_pgoff,
                        size,
                        vma->vm_page_prot)) {
        pr_info("%s: mapping failed\n", __func__);
        return -EINVAL;
    }
    return 0;
}


static struct file_operations dma_demo_fops = {
	.owner          = THIS_MODULE,
    .open           = dma_demo_open,
    .release        = dma_demo_close,
    .mmap           = dma_demo_mmap,
};


//----------------miscdevice------------------
static struct miscdevice dma_demo = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVICE_NAME,
	.fops  = &dma_demo_fops,
};

static int __init dma_demo_init(void)
{
	int err;
	//pr_info("%s\n", __func__);

	err = misc_register(&dma_demo);
    if (err)
        return err;

	//pr_info("   %p register: %d, dev: %p\n", &dma_demo, err, dma_demo.this_device);

	return 0;
}

static void __exit dma_demo_exit(void)
{
	misc_deregister(&dma_demo);
	//pr_info("%s\n", __func__);
}

module_init(dma_demo_init);
module_exit(dma_demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("William <william_jeng@avision.com.tw");

