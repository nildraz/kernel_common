/*
 *  Copyright (C) 2018 NUFRONT Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/printk.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/vmalloc.h>

#include "efuse.h"

#define DRIVER_NAME "efuse"

#define to_efuse(x) container_of(x, struct efusedevice, misc)

static int efuse_generic_read_buf(struct efusedevice *edev,
	char *buf, size_t count, u32 offset)
{
	int i;

	for (i = 0; i < count; i++) {
		if (edev->ops->read_byte(edev, buf + i, offset + i) < 0)
			return -EIO;
	}
	return i;
}

static int efuse_dev_read(struct efusedevice *edev,
	char __user *buf, size_t count, u32 offset)
{
	char *kbuf;
	int res = 0;

	kbuf = vmalloc(count);
	if (!kbuf)
		return -ENOMEM;

	mutex_lock(&edev->lock);

	if (edev->ops->pre_read)
		edev->ops->pre_read(edev);

	if (edev->ops->read_buf)
		res = edev->ops->read_buf(edev, kbuf, count, offset);
	else
		res = efuse_generic_read_buf(edev, kbuf, count, offset);

	if (edev->ops->post_read)
		edev->ops->post_read(edev);

	mutex_unlock(&edev->lock);

	if (copy_to_user(buf, kbuf, count))
		res = -EFAULT;
	vfree(kbuf);
	return res;
}

static ssize_t efuse_file_read(struct file *file, char __user *buf,
	size_t count, loff_t *ppos)
{
	struct efusedevice *edev = to_efuse(file->private_data);
	size_t available = edev->capacity;
	loff_t pos = *ppos;
	size_t res;

	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	res = efuse_dev_read(edev, buf, count, pos);
	if (res < 0)
		return res;
	*ppos = pos + res;
	return res;
}

static int efuse_generic_program_buf(struct efusedevice *edev,
	const char *buf, size_t count, u32 offset)
{
	int i;

	for (i = 0; i < count; i++) {
		if (edev->ops->program_byte(edev, buf[i], offset + i) < 0)
			return -EIO;
	}
	return i;
}

static int efuse_dev_write(struct efusedevice *edev,
	const char __user *buf, size_t count, u32 offset)
{
	char *kbuf;
	int res = 0;

	kbuf = vmalloc(count);
	if (!kbuf)
		return -ENOMEM;

	if (copy_from_user(kbuf, buf, count)) {
		res = -EFAULT;
		goto error;
	}

	mutex_lock(&edev->lock);

	if (edev->ops->pre_program)
		edev->ops->pre_program(edev);

	if (edev->ops->program_buf)
		res = edev->ops->program_buf(edev, kbuf, count, offset);
	else
		res = efuse_generic_program_buf(edev, kbuf, count, offset);

	if (edev->ops->post_program)
		edev->ops->post_program(edev);

	mutex_unlock(&edev->lock);
error:
	vfree(kbuf);
	return res;
}

static ssize_t efuse_file_write(struct file *file, const char __user *buf,
	size_t count, loff_t *ppos)
{
	struct efusedevice *edev = to_efuse(file->private_data);
	size_t available = edev->capacity;
	loff_t pos = *ppos;
	size_t res;

	if (!edev->ops->getwp || edev->ops->getwp(edev))
		return -EINVAL;
	if (pos < 0)
		return -EINVAL;
	if (pos >= available || !count)
		return 0;
	if (count > available - pos)
		count = available - pos;
	res = efuse_dev_write(edev, buf, count, pos);
	if (res < 0)
		return res;
	*ppos = pos + res;
	return res;
}

static loff_t efuse_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct efusedevice *edev =
		to_efuse(file->private_data);
	size_t available = edev->capacity;

	return generic_file_llseek_size(file, offset, whence,
			available,
			available - file->f_pos);
}

static const struct file_operations efuse_op = {
	.owner		= THIS_MODULE,
	.open		= generic_file_open,
	.llseek		= efuse_file_llseek,
	.read		= efuse_file_read,
	.write		= efuse_file_write,
};

int efuse_register(struct efusedevice *edev)
{
	mutex_init(&edev->lock);
	edev->misc.fops = &efuse_op;
	edev->misc.name = edev->name? edev->name : DRIVER_NAME;
	edev->misc.minor = MISC_DYNAMIC_MINOR;
	if (misc_register(&edev->misc))
		return EINVAL;
	return 0;
}

int efuse_unregister(struct efusedevice *edev)
{
	misc_deregister(&edev->misc);
	mutex_destroy(&edev->lock);
	return 0;
}

