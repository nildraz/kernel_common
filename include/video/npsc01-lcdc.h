/*
 * include/video/npsc01_lcdfb.h
 *
 * Copyright (C) 2016 Nufront Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __NPSC01_LCDC_H__
#define __NPSC01_LCDC_H__

#include <linux/notifier.h>
#include <linux/completion.h>
#include "../../drivers/staging/android/sync.h"

#define N7TL_LCDC_BASE	0x06250000

#define	LCDC_DSPC_CTRL			0x00
	#define TEST_COLORBAR			BIT(4)
	#define VGA_ENABLE			BIT(0)
#define LCDC_VERSION			0x04
#define LCDC_INT_STATUS			0x10
	#define VERT_FLAG			BIT(24)
#define OVERALL_UNDERFLOW_FLAG  BIT(12)
#define LCDC_INT_CLR			0x14
#define LCDC_INT_MASK			0x18

#define LCDC_RD_CTRL			0x100
#define LCDC_RD_PTRSEL			0x104
	#define RGB_PTRSEL_MASK			(BIT(0)|BIT(1))
#define LCDC_RD_AXI			0x10c
#define LCDC_RD_AEMPT			0x140

#define LCDC_DPI_CTRL			0x300
	#define DPI_RGB_PXLCLKEN		BIT(13)
	#define DPI_LVDS_PXLCLKEN		BIT(12)
	#define DPI_MD_SHIFT			(8)
	#define DPI_MD_MASK			(0x700)

#define LCDC_HV_SIZE			0x310
#define LCDC_H_PORCH			0x320
#define LCDC_H_WIDTH			0x324
#define LCDC_V_PORCH			0x330
#define LCDC_V_WIDTH			0x334

#define LCDC_RGB_PTR0			0x410
#define LCDC_RGB_PTR1			0x414
#define LCDC_RGB_PTR2			0x418
#define LCDC_RGB_FORMAT			0x440
#define LCDC_RGB_ORI_SIZE		0x450
	#define UI_ORI_SIZE_V_MASK		(0x1fff << 16)
	#define UI_ORI_SIZE_V_SHIFT		16
	#define UI_ORI_SIZE_H_MASK		(0x1ffc << 0)
	#define UI_ORI_SIZE_H_SHIFT		0
#define LCDC_RGB_CROP_OFFSET		0x454
	#define UI_CROP_OFF_V_MASK		(0x1fff << 16)
	#define UI_CROP_OFF_V_SHIFT		16
	#define UI_CROP_OFF_H_MASK		(0x1fff << 0)
	#define UI_CROP_OFF_H_SHIFT		0
#define LCDC_RGB_CROP_SIZE		0x458
	#define UI_CROP_SIZE_V_MASK		(0x1fff << 16)
	#define UI_CROP_SIZE_V_SHIFT		16
	#define UI_CROP_SIZE_H_MASK		(0x1fff << 0)
	#define UI_CROP_SIZE_H_SHIFT		0

#define LCDC_CP_CTRL			0xb00
	#define CP_DITHER_EN			BIT(8)

#define LCDC_DEBUG			0xe00
#define LCDC_RGB_ALM_FULL		0xe10

#define REG_NUM				1024

#define SCM_PAD_E_0			0x78
#define SCM_PAD_E_1			0x7c

#define FB_LCDC_SET_CONFIG		_IOW('F', 0x21, struct npsc01_lcdc_config)
#define FB_LCDC_DISABLE			_IOW('F', 0x22, int)
#define FB_LCDC_ENABLE			_IOW('F', 0x23, int)
#define FB_LCDC_WRITEBACK               _IOW('F', 0x24, int)
#define FB_LCDC_GET_REG			_IOR('F', 0x25, int)
#define FB_LCDC_SET_MCLK		_IOW('F', 0x26, int)
#define FB_LCDC_SET_PIXCLK		_IOW('F', 0x27, int)
#define FB_LCDC_UPDATE_CONFIG		_IOW('F', 0x28, struct npsc01_lcdc_config)
#define FB_AD_ENABLE			_IOW('F', 0x29, unsigned int)
#define FB_AD_DISABLE			_IOW('F', 0x2A, unsigned int)
#define FB_AD_CALIB_ON_OFF		_IOW('F', 0x2B, unsigned int)
#define FB_AD_SET_REG			_IOW('F', 0x2C, unsigned int[2])
#define FB_WAIT_VSYNC			_IOW('F', 0x35, unsigned int)
#define FB_GET_SRC_HEIGHT		_IOW('F', 0x36, unsigned int)
#define FB_CREATE_VIDEO_FENCE		_IOWR('F', 0x38, struct fb_sync_create_fence_data)

enum Sample {
	ARGB8888 = 0,
	ABGR8888,
	XRGB8888,
	XBGR8888,
	ABGR1555,
	RGBA5551,
	ARGB4444,
	RGB565,
	BGR565,
	YUV420,
	YUV420T,
};

enum power_type {
	PANEL_POWER_ON = 0,
	PANEL_POWER_OFF,
	PANEL_BACKLIGHT_ON,
	PANEL_BACKLIGHT_OFF,
};

struct pixclk_pll_mapping
{
	unsigned long pixclock;
	unsigned long pll_rate;
	unsigned long divider;
};

struct fb_sync_timeline {
	struct sync_timeline obj;
	u32 value;
};

struct fb_sync_pt {
	struct sync_pt pt;
	u32 value;
};

struct fb_sync_create_fence_data {
	int fence; /* fd of new fence */
	int inc;
};

struct npsc01_lcdc_config
{
	int id_major;
	int id_minor;

	/* RGB layer */
	int ui_ori_h, ui_ori_v;
	int ui_crop_h, ui_crop_v;
	int ui_crop_off_h, ui_crop_off_v;
	int ui_scale_h, ui_scale_v;
	enum Sample ui_sample;
	int ui_filt_en;
	int ui_filt0, ui_filt1, ui_filt2;
	int ui_alpha_src;
	int ui_z;
	int ui_gm_en;
	short ui_gm_a[16], ui_gm_b[16];
	int ui_dth_en;

	/* Video layer */
	int vi_ori_h, vi_ori_v;
	int vi_crop_h, vi_crop_v;
	int vi_crop_off_h, vi_crop_off_v;
	int vi_stride;
	int vi_scale_h, vi_scale_v;
	enum Sample vi_sample;
	int vi_rotation;
	int vi_filt_en;
	int vi_filt0, vi_filt1, vi_filt2;
	int vi_alpha_src;
	int vi_z;
	int vi_gm_en;
	short vi_gm_a[16], vi_gm_b[16];
	int vi_dth_en;

	/* Common path */
	int comp_size_h, comp_size_v;
	int comp_ui_off_h, comp_ui_off_v;
	int comp_vi_off_h, comp_vi_off_v;
	int comp_gm_en;
	short comp_gm_a[16], comp_gm_b[16];
	int comp_dth_en, dpi_md;

	int burst_len;

	/* Cursor layer */
	int cursor_size_h, cursor_size_v;
	int cursor_off_h, cursor_off_v;
	int cursor_z;
};

struct npsc01_par
{
	void __iomem *mmio;
	void __iomem *mmio_ad;

	unsigned int irq;
/*	unsigned int err_irq;
	unsigned int vert_irq;
	unsigned int horiz_irq;
*/
	struct clk *pxlclk;
	const char *pxlclk_name;

	struct clk *pclk;
	const char *pclk_name;
/*
	struct clk *hclk;
	const char *hclk_name;

	struct clk *pll;
	const char *pll_name;

	struct clk *mclk;
	const char *mclk_name;
*/
	struct reset_control *reset;

	struct npsc01_lcdc_config	config;
	unsigned int regsave[REG_NUM];
	struct delayed_work ad_work;
	unsigned int nr_buffers;

	struct device_node *display_interface;
	const char *panel;

	ktime_t vsync_time;
	struct completion vsync;
	struct fb_sync_timeline *fb_timeline;

	unsigned int lcdc_enabled;
	struct notifier_block lcdfb_notif;
};

#endif
