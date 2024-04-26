/*
 * u_printer.h
 *
 * Utility definitions for the printer function
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef U_PRINTER_H
#define U_PRINTER_H

#include <linux/usb/composite.h>

#define PNP_STRING_LEN			1024

struct f_printer_opts {
	struct usb_function_instance	func_inst;
	int				minor;
	char				pnp_string[PNP_STRING_LEN];
	unsigned			q_len;

	/*
	 * Protect the data from concurrent access by read/write
	 * and create symlink/remove symlink
	 */
	struct mutex			lock;
	int				refcnt;
};

#define CONFIGFS_ATTR(_pfx, _name)                      \
static struct configfs_attribute _pfx##attr_##_name = { \
		.ca_name        = __stringify(_name),           \
		.ca_mode        = S_IRUGO | S_IWUSR,            \
		.ca_owner       = THIS_MODULE,                  \
}

#endif /* U_PRINTER_H */
