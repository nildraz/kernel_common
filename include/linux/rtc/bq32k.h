/*
 * include/linux/rtc/bq32k.h
 *
 * Copyright (C) 2015 InnoComm Mobile Technology Corp.
 * Author: James Wu <james.wu@innocomm.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#ifndef _LINUX_RTC_BQ32K_H
#define _LINUX_RTC_BQ32K_H

#include <linux/types.h>
#include <linux/rtc.h>

struct bq32k_rtc_data {
	struct rtc_time	default_time;
};

#endif /* _LINUX_RTC_BQ32K_H */

