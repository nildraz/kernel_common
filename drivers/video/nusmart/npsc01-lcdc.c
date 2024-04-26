#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/dma-mapping.h>

#include <asm/uaccess.h>

#include <mach/pinctrl-nufront.h>

#include <video/npsc01-lcdc.h>
#include "panel/npsc01_panel.h"

#define	NS_FB_NAME	"nusmartfb"
#define TRACE	pr_err("%s:%d\n", __func__, __LINE__);

#define FPGA_VERIFY	0

#define DBG_ERR		0x8
#define DBG_WARN	0x4
#define DBG_INFO	0x2
#define DBG_DEBUG	0x1

/*
 * Modify /sys/module/npsc01_lcd/parameters/dbg_level
 * to regulate print level of this driver
 */
static unsigned int dbg_level = DBG_ERR | DBG_WARN | DBG_INFO;
module_param_named(dbg_level, dbg_level, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define lcdc_err(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_ERR)					\
		printk(KERN_ERR "lcd err: " fmt, ##args);		\
	} while(0)

#define lcdc_warn(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_WARN)				\
		printk(KERN_WARNING "lcd warn: " fmt, ##args);		\
	} while(0)

#define lcdc_info(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_INFO)				\
		printk(KERN_INFO "lcd info: " fmt, ##args);		\
	} while(0)

#define lcdc_dbg(fmt, args...)						\
	do {								\
		if(dbg_level & DBG_DEBUG)				\
		printk(KERN_DEBUG "lcd dbg: " fmt, ##args);		\
	} while(0)

#define set_reg_bits(reg_addr, bits)					\
	do {								\
		unsigned int val = readl(reg_addr);			\
		val |= bits;						\
		writel(val, reg_addr);					\
	} while (0)

#define clr_reg_bits(reg_addr, bits)					\
	do {								\
		unsigned int val = readl(reg_addr);			\
		val &= ~bits;						\
		writel(val, reg_addr);					\
	} while (0)

static int (*panel_power_init) (void);
static int (*panel_power_on) (void);
static int (*panel_power_off) (void);

static int npsc01_fb_save(struct fb_info *info);
static int npsc01_fb_restore(struct fb_info *info);
static int npsc01_fb_clk_set(struct fb_info *info);
static int npsc01_fb_clk_enable_disable(struct fb_info *info, bool enable);

static void reg_set_bit(void __iomem *addr, unsigned int bit)
{
	unsigned int data = readl(addr);
	data |= bit;
	writel(data, addr);
}

static void reg_clr_bit(void __iomem *addr, unsigned int bit)
{
	unsigned int data = readl(addr);
	data &= ~bit;
	writel(data, addr);
}

static void npsc01_lcdc_enable(struct fb_info *info)
{
	struct npsc01_par *par = info->par;

	TRACE;
	reg_set_bit(par->mmio + LCDC_DPI_CTRL, DPI_LVDS_PXLCLKEN);//DPI_RGB_PXLCLKEN);// | DPI_LVDS_PXLCLKEN);
	reg_set_bit(par->mmio + LCDC_DSPC_CTRL, VGA_ENABLE);
}

static void npsc01_lcdc_disable(struct fb_info *info)
{
	struct npsc01_par *par = info->par;

	TRACE;
	reg_clr_bit(par->mmio + LCDC_DPI_CTRL, DPI_LVDS_PXLCLKEN);//DPI_RGB_PXLCLKEN);// | DPI_LVDS_PXLCLKEN);
	reg_clr_bit(par->mmio + LCDC_DSPC_CTRL, VGA_ENABLE);
}

static int npsc01_fb_idle_in(struct fb_info *info) {
	int ret;
	struct npsc01_par *par = info->par;

	if (!par->lcdc_enabled) {
		lcdc_warn("lcdc_enabled:%d suspend is ignored.\n", par->lcdc_enabled);
		return 0;
	} else
		par->lcdc_enabled = 0;

	npsc01_fb_save(info);
	npsc01_lcdc_disable(info);
	disable_irq(par->irq);
	npsc01_fb_clk_enable_disable(info, false);
	lcdc_info("driver is in idle mode.\n");
	return 0;
}
static int npsc01_fb_idle_out(struct fb_info *info) {

	int ret;
	struct npsc01_par *par = info->par;

	if (par->lcdc_enabled) {
		lcdc_warn("lcdc_enabled:%d resume is ignored.\n", par->lcdc_enabled);
		return 0;
	} else
		par->lcdc_enabled = 1;

	npsc01_fb_clk_enable_disable(info, true);
	npsc01_fb_restore(info);
	enable_irq(par->irq);

	lcdc_info("driver is out idle mode.\n");
	return 0;
}

static int npsc01_fb_suspend(struct fb_info *info)
{
	int ret;
	struct npsc01_par *par = info->par;

	if (!par->lcdc_enabled) {
		lcdc_warn("lcdc_enabled:%d suspend is ignored.\n", par->lcdc_enabled);
		return 0;
	} else
		par->lcdc_enabled = 0;

	ret = panel_power_off();
	if (ret) lcdc_err("panel_power_off error.\n");

	npsc01_fb_save(info);
	npsc01_lcdc_disable(info);
	//disable_irq(par->err_irq);
	disable_irq(par->irq);
	npsc01_fb_clk_enable_disable(info, false);
#ifdef CONFIG_RESET_CONTROLLER
//	reset_control_assert(par->reset);
#endif

	lcdc_info("driver is suspended.\n");
	return 0;
}

static int npsc01_fb_resume(struct fb_info *info)
{
	int ret;
	struct npsc01_par *par = info->par;

	if (par->lcdc_enabled) {
		lcdc_warn("lcdc_enabled:%d resume is ignored.\n", par->lcdc_enabled);
		return 0;
	} else
		par->lcdc_enabled = 1;

#ifdef CONFIG_RESET_CONTROLLER
//	reset_control_deassert(par->reset);
#endif
	npsc01_fb_clk_enable_disable(info, true);

	npsc01_fb_restore(info);
	//enable_irq(par->err_irq);
	enable_irq(par->irq);

	ret = panel_power_on();
	if (ret) lcdc_err("panel_power_on error.\n");

	lcdc_info("driver is resumed.\n");
	return 0;
}

static int npsc01_fb_plat_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	return npsc01_fb_suspend(info);
}

static int npsc01_fb_plat_resume(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	return npsc01_fb_resume(info);
}

static int npsc01_fb_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct fb_event *fbevent = data;
	struct fb_info *info = fbevent->info;

	switch(event) {

	case FB_EVENT_SUSPEND:
		npsc01_fb_suspend(info);
		break;
		
	case FB_EVENT_RESUME:
		npsc01_fb_resume(info);
		break;

	case FB_EVENT_IDLE_IN:
		npsc01_fb_idle_in(info);
		break;

	case FB_EVENT_IDLE_OUT:
		npsc01_fb_idle_out(info);
		break;
	}
	return 0;
}

static int npsc01_fb_notifier_register(struct fb_info *info)
{
	struct npsc01_par *par = info->par;

	memset(&par->lcdfb_notif, 0, sizeof(par->lcdfb_notif));
	par->lcdfb_notif.notifier_call = npsc01_fb_notifier_callback;
	par->lcdfb_notif.priority = 3;

	return fb_register_client(&par->lcdfb_notif);
}

static struct fb_videomode* npsc01_lcdc_find_vga_mode(struct fb_info *info)
{
	int i, namelen;
	int xres = 0, yres = 0, refresh = 0;
	struct npsc01_par *par = info->par;

	namelen = strlen(par->panel);

	xres = simple_strtol(par->panel, NULL, 10);
	for (i = namelen-1; i >= 0; i--) {
		switch (par->panel[i]) {
			case '@':
				namelen = i;
				refresh = simple_strtol(&((par->panel)[i+1]), NULL, 10);
				break;

			case 'x':
				yres = simple_strtol(&((par->panel)[i+1]), NULL, 10);
				break;
		}
	}
	lcdc_info("Specified VGA resolution in DTB: %dx%d@%d\n", xres, yres, refresh);
	for (i = 0; i < vga_size; i++)
		if (vga_mode[i].xres == xres && vga_mode[i].yres == yres &&
				vga_mode[i].refresh == refresh) {
			lcdc_info("Match the %d vga_mode: { %s, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d }\n", i,
					vga_mode[i].name, vga_mode[i].refresh,
					vga_mode[i].xres, vga_mode[i].yres, vga_mode[i].pixclock,
					vga_mode[i].left_margin, vga_mode[i].right_margin,
					vga_mode[i].upper_margin, vga_mode[i].lower_margin,
					vga_mode[i].hsync_len, vga_mode[i].vsync_len);

			return &vga_mode[i];
		}

	lcdc_err("Cann't match DTB VGA resolution in vga_mode\n");
	return NULL;
}

static struct fb_videomode* npsc01_lcdc_find_nonvga_mode(struct fb_info *info)
{
	char xxxx_mode[64];
	struct npsc01_par *par = info->par;

	sprintf(xxxx_mode, "%s_mode", par->panel);
	lcdc_info("xxxx_mode = %s\n", xxxx_mode);

	return (void*)kallsyms_lookup_name(xxxx_mode);
}

static struct fb_videomode* npsc01_lcdc_find_mode(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	unsigned int hsize, vsize, bpp;
	char *option;
	int ret = 0;

	if (!strcmp(par->display_interface->name, "VGA") ||
			!strcmp(par->display_interface->name, "vga")) {
		/* Get option from bootargs */
		fb_get_options(NS_FB_NAME, &option);
		if (option) {
			sscanf(option, "%ux%u-%u", &hsize, &vsize, &bpp);
			ret = fb_find_mode(&info->var, info, option, vga_mode,
					vga_size, NULL, bpp);
			if (ret == 1 || ret == 2) {
				fb_var_to_videomode(&current_mode, &info->var);
				return &current_mode;
			}
		}
		return npsc01_lcdc_find_vga_mode(info);
	} else {
		return &npsc01_lvds_mode;
//		return npsc01_lcdc_find_nonvga_mode(info);
	}
}

static void set_options(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	struct npsc01_lcdc_config *config = &par->config;

	fb_videomode_to_var(&info->var, npsc01_lcdc_find_mode(info));
	fb_var_to_videomode(&current_mode, &info->var);
	info->mode = &npsc01_lvds_mode;
	if (!info->mode) {
		lcdc_err("Can't find fb_info mode.\n");
		return;
	}

	info->var.nonstd	= 0;
	info->var.accel_flags	= 0;
	info->var.height	= -1;
	info->var.width		= -1;
	info->var.activate	= FB_ACTIVATE_NOW;
	info->var.vmode		= FB_VMODE_NONINTERLACED;

	info->var.xres_virtual	= info->var.xres;
	info->var.yres_virtual	= info->var.yres * 3;

	info->var.bits_per_pixel = 32;

	strcpy(info->fix.id, NS_FB_NAME);
	info->fix.type		= FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux	= 0;
	info->fix.xpanstep	= 0;
	info->fix.ypanstep	= 1;
	info->fix.ywrapstep	= 0;
	info->fix.visual	= FB_VISUAL_TRUECOLOR;
	info->fix.accel		= FB_ACCEL_NONE;
	info->fix.line_length	= info->var.xres * info->var.bits_per_pixel >> 3;

	info->flags		= FBINFO_DEFAULT;

	config->ui_ori_h = info->var.xres;
	config->ui_ori_v = info->var.yres;
	config->ui_crop_h = info->var.xres;
	config->ui_crop_v = info->var.yres;
	config->ui_scale_h = info->var.xres;
	config->ui_scale_v = info->var.yres;
	config->ui_sample = ARGB8888;

	config->vi_ori_h = 0;
	config->vi_ori_v = 0;

	config->cursor_size_h = config->cursor_size_v = 1;
	config->cursor_off_h = config->cursor_off_v = 0;

	config->comp_size_h = info->var.xres;
	config->comp_size_v = info->var.yres;
	config->comp_ui_off_h = config->comp_ui_off_v = 0;
}

static void __maybe_unused ui_crop(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	struct npsc01_lcdc_config *config = &par->config;
	int data;

	if(config->ui_ori_h && config->ui_ori_v) {
		data = ((config->ui_ori_v << UI_ORI_SIZE_V_SHIFT) & UI_ORI_SIZE_V_MASK) |
			((config->ui_ori_h << UI_ORI_SIZE_H_SHIFT) & UI_ORI_SIZE_H_MASK);
		writel(data, par->mmio + LCDC_RGB_ORI_SIZE);
	}

	data = ((config->ui_crop_off_h << UI_CROP_OFF_H_SHIFT) & UI_CROP_OFF_H_MASK) |
	       ((config->ui_crop_off_v << UI_CROP_OFF_V_SHIFT) & UI_CROP_OFF_V_MASK);
	writel(data, par->mmio + LCDC_RGB_CROP_OFFSET);

	data = ((config->ui_crop_h << UI_CROP_SIZE_H_SHIFT) & UI_CROP_SIZE_H_MASK) |
	       ((config->ui_crop_v << UI_CROP_SIZE_V_SHIFT) & UI_CROP_SIZE_V_MASK);
	writel(data, par->mmio + LCDC_RGB_CROP_SIZE);


	/* burst len */
	if (config->burst_len) {
		data = readl(par->mmio + LCDC_RD_AXI);
		data &= ~0xf00;
		data |= (config->burst_len << 8);
		writel(data, par->mmio + LCDC_RD_AXI);
	}
}

static void __maybe_unused ui_format(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	struct npsc01_lcdc_config *config = &par->config;
	int data = readl(par->mmio + LCDC_RGB_FORMAT);

	data &= ~0x30100;

	switch (config->ui_sample) {
	case ARGB8888:
		break;
	case ABGR8888:
		data |= (1 << 8);
		break;
	case XRGB8888:
		break;
	case XBGR8888:
		data |= (1 << 8);
		break;
	case ABGR1555:
		data |= (2 << 16);
		break;
	case RGBA5551:
		data |= (2 << 16) | (1 << 8);
		break;
	case ARGB4444:
		data |= (3 << 16);
		break;
	case RGB565:
		data |= (1 << 16);
		break;
	case BGR565:
		data |= (1 << 16) | (1 << 8);
		break;
	default:
		break;
	}
	writel(data, par->mmio + LCDC_RGB_FORMAT);
}

static void __maybe_unused cp_dither(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	struct npsc01_lcdc_config *config = &par->config;

	if (!config->ui_dth_en) return;
	//lcdc_err("------dither-----en------");
	reg_set_bit(par->mmio + LCDC_CP_CTRL, CP_DITHER_EN);
	reg_clr_bit(par->mmio + LCDC_DPI_CTRL, DPI_MD_MASK);
	reg_set_bit(par->mmio + LCDC_DPI_CTRL, config->dpi_md<<DPI_MD_SHIFT);
}

static int check_constraints(struct npsc01_lcdc_config *input)
{
	/*** 5.2.9 ***/
	if (input->vi_ori_h && input->vi_ori_v) {
		/* 1 */
		if (input->vi_crop_off_h + input->vi_crop_h > input->vi_ori_h) {
			lcdc_err("vi_crop_off_h + vi_crop_h > vi_ori_h\n");
			return -1;
		}
		/* 2 */
		if (input->vi_crop_off_v + input->vi_crop_v > input->vi_ori_v) {
			lcdc_err("vi_crop_off_v + vi_crop_v > vi_ori_v\n");
			return -1;
		}
	}
	/* 3,4 is defined in video_scale */

	/* 1 */
	if (input->ui_ori_h && input->ui_ori_v) {
		if (input->ui_scale_h + input->comp_ui_off_h > input->comp_size_h) {
			lcdc_err("ui_scale_h + comp_ui_off_h > comp_size_h\n");
			return -1;
		}
		if (input->ui_scale_v + input->comp_ui_off_v > input->comp_size_v) {
			lcdc_err("ui_scale_v + comp_ui_off_v > comp_size_v\n");
			return -1;
		}
	}
	if (input->vi_ori_h && input->vi_ori_v) {
		if (input->vi_scale_h + input->comp_vi_off_h > input->comp_size_h) {
			lcdc_err("vi_scale_h + comp_vi_off_h > comp_size_h\n");
			return -1;
		}
		if (input->vi_scale_v + input->comp_vi_off_v > input->comp_size_v) {
			lcdc_err("vi_scale_v + comp_vi_off_v > comp_size_v\n");
			return -1;
		}
	}

	/*** 5.2.10 ***/
	/* 1 */
	if (input->ui_ori_h && input->ui_ori_v) {
		switch (input->ui_sample) {
		case ARGB8888:
		case ABGR8888:
		case XRGB8888:
		case XBGR8888:
			if (input->ui_ori_h % 4 != 0) {
				lcdc_err("ui_ori_h[1:0] != 0\n");
				return -1;
			}
			break;
		case ABGR1555:
		case RGBA5551:
		case ARGB4444:
		case RGB565:
		case BGR565:
			if (input->ui_ori_h % 8 != 0) {
				lcdc_err("ui_ori_h[2:0] != 0\n");
				return -1;
			}
			break;
		default:
			break;
		}
	}
	if (input->vi_ori_h && input->vi_ori_v) {
		if (input->vi_rotation == 90 || input->vi_rotation == 270) {
			switch (input->vi_sample) {
			case ARGB8888:
			case ABGR8888:
			case XRGB8888:
			case XBGR8888:
				if (input->vi_ori_h % 8 != 0) {
					lcdc_err("vi_ori_h[2:0] != 0\n");
					return -1;
				}
				break;
			case ABGR1555:
			case RGBA5551:
			case ARGB4444:
			case RGB565:
			case BGR565:
				if (input->vi_ori_h % 16 != 0) {
					lcdc_err("vi_ori_h[3:0] != 0\n");
					return -1;
				}
				break;
			case YUV420:
				if (input->vi_ori_h % 32 != 0) {
					lcdc_err("vi_ori_h[4:0] != 0\n");
					return -1;
				}
				break;
			default:
				break;
			}
		} else {
			switch (input->vi_sample) {
			case ARGB8888:
			case ABGR8888:
			case XRGB8888:
			case XBGR8888:
				if (input->vi_ori_h % 4 != 0) {
					lcdc_err("vi_ori_h[1:0] != 0\n");
					return -1;
				}
				break;
			case ABGR1555:
			case RGBA5551:
			case ARGB4444:
			case RGB565:
			case BGR565:
				if (input->vi_ori_h % 8 != 0) {
					lcdc_err("vi_ori_h[2:0] != 0\n");
					return -1;
				}
				break;
			case YUV420:
				if (input->vi_ori_h % 16 != 0) {
					lcdc_err("vi_ori_h[3:0] != 0\n");
					return -1;
				}
				break;
			default:
				break;
			}
		}
	}

	/* 2 */
	if (input->vi_ori_h && input->vi_ori_v && input->vi_sample == YUV420T) {
		if (input->vi_ori_h % 8 != 0 || input->vi_ori_v % 8 != 0) {
			lcdc_err("vi_ori_h[2:0] != 0 || vi_ori_v[2:0] != 0\n");
			return -1;
		}
	}

	/* 3,4 is defined in video_format */
	/* 5 is defined in writeback */

	/* 6 */
	if (input->ui_ori_h && input->ui_ori_v) {
		if (input->ui_scale_h > input->ui_ori_h) {
			if (input->ui_ori_h % 2 != 0) {
				lcdc_err("5.2.10.6 ui_ori_h[0] != 0\n");
				return -1;
			}
		} else if (input->ui_scale_h < input->ui_ori_h) {
			if (input->ui_scale_h % 2 != 0) {
				lcdc_err("5.2.10.6 ui_scale_h[0] != 0\n");
				return -1;
			}
		}
	}
	if (input->vi_ori_h && input->vi_ori_v) {
		if (input->vi_scale_h > input->vi_crop_h) {
			if (input->vi_rotation == 90 || input->vi_rotation == 270) {
				if (input->vi_crop_v % 2 != 0) {
					lcdc_err("5.2.10.6 vi_crop_v[0] != 0\n");
					return -1;
				}
			} else {
				if (input->vi_crop_h % 2 != 0) {
					lcdc_err("5.2.10.6 vi_crop_h[0] != 0\n");
					return -1;
				}
			}
		} else if (input->vi_scale_h < input->vi_crop_h) {
			if (input->vi_scale_h % 2 != 0) {
				lcdc_err("5.2.10.6 vi_scale_h[0] != 0\n");
				return -1;
			}
		}
	}

	/* 7 */
	if (input->vi_ori_h && input->vi_ori_v) {
		if (input->vi_sample == YUV420 || input->vi_sample == YUV420T) {
			if (input->vi_crop_off_h % 2 != 0) {
				lcdc_err("5.2.10.7 vi_crop_off_h[0] != 0\n");
				return -1;
			}
			if (input->vi_crop_off_v % 2 != 0) {
				lcdc_err("5.2.10.7 vi_crop_off_v[0] != 0\n");
				return -1;
			}
			if (input->vi_rotation == 90 || input->vi_rotation == 270) {
				if (input->vi_crop_v % 4 != 0) {
					lcdc_err("5.2.10.7 vi_crop_v[1:0] != 0\n");
					return -1;
				}
				if ((input->vi_crop_h / input->vi_stride) % 2 != 0) {
					lcdc_err("5.2.10.7 (vi_crop_h/vi_stride)%%2 != 0\n");
					return -1;
				}
			} else {
				if (input->vi_crop_h % 4 != 0) {
					lcdc_err("5.2.10.7 vi_crop_h[1:0] != 0\n");
					return -1;
				}
				if ((input->vi_crop_v / input->vi_stride) % 2 != 0) {
					lcdc_err("5.2.10.7 (vi_crop_v/vi_stride)%%2 != 0\n");
					return -1;
				}
			}
		}
	}

	/* 11 */
	if (input->vi_ori_h && input->vi_ori_v &&
	    input->vi_rotation != 90 && input->vi_rotation != 270 &&
	    input->vi_sample != YUV420T) {
		switch (input->vi_sample) {
		case YUV420:
			if (!input->burst_len) {
				if (input->vi_crop_h <= 32) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*16\n");
					return -1;
				}
			} else {
				if (input->vi_crop_h <= (input->burst_len+1)*16) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*16\n");
					return -1;
				}
			}
		case ARGB8888:
		case ABGR8888:
		case XRGB8888:
		case XBGR8888:
			if (!input->burst_len) {
				if (input->vi_crop_h <= 8) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*4\n");
					return -1;
				}
			} else {
				if (input->vi_crop_h <= (input->burst_len+1)*4) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*4\n");
					return -1;
				}
			}
			break;
		default:
			if (!input->burst_len) {
				if (input->vi_crop_h <= 16) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*8\n");
					return -1;
				}
			} else {
				if (input->vi_crop_h <= (input->burst_len+1)*8) {
					lcdc_err("5.2.10.11 vi_crop_h <= (arblen+1)*8\n");
					return -1;
				}
			}
			break;
		}
	}
	if (input->ui_ori_h && input->ui_ori_v) {
		switch (input->ui_sample) {
		case ARGB8888:
		case ABGR8888:
		case XRGB8888:
		case XBGR8888:
			if (!input->burst_len) {
				if (input->ui_ori_h <= 8) {
					lcdc_err("5.2.10.11 ui_ori_h <= (arblen+1)*4\n");
					return -1;
				}
			} else {
				if (input->ui_ori_h <= (input->burst_len+1)*4) {
					lcdc_err("5.2.10.11 ui_ori_h <= (arblen+1)*4\n");
					return -1;
				}
			}
			break;
		case ABGR1555:
		case RGBA5551:
		case ARGB4444:
		case RGB565:
		case BGR565:
			if (!input->burst_len) {
				if (input->ui_ori_h <= 16) {
					lcdc_err("5.2.10.11 ui_ori_h <= (arblen+1)*8\n");
					return -1;
				}
			} else {
				if (input->ui_ori_h <= (input->burst_len+1)*8) {
					lcdc_err("5.2.10.11 ui_ori_h <= (arblen+1)*8\n");
					return -1;
				}
			}
			break;
		default:
			break;
		}
	}

	/* 13 */
	if (input->vi_ori_h && input->vi_ori_v) {
		if (input->vi_rotation == 90 || input->vi_rotation == 270) {
			if (input->vi_crop_h <= 2*input->vi_stride) {
				lcdc_err("5.2.10.13 vi_crop_h <= 2*vi_stride\n");
				return -1;
			}
		} else {
			if (input->vi_crop_v <= 2*input->vi_stride) {
				lcdc_err("5.2.10.13 vi_crop_h <= 2*vi_stride\n");
				return -1;
			}
		}
	}

	/* 14 */
	if (input->ui_z || input->vi_z || input->cursor_z) {
		if (input->ui_z == input->vi_z ||
		    input->vi_z == input->cursor_z ||
		    input->cursor_z == input->ui_z) {
			lcdc_err("5.2.10.14 ui_z != vi_z != cursor_z\n");
			return -1;
		}
	}

	/* 18 */
	if (input->vi_ori_h && input->vi_ori_v && input->vi_sample == YUV420) {
		if (input->vi_ori_v % 2 != 0) {
			lcdc_err("5.2.10.18 vi_ori_v[0] != 0\n");
			return -1;
		}
	}

	return 0;
}

static int set_config(struct fb_info *info, struct npsc01_lcdc_config *config)
{
	struct npsc01_par *par = info->par;
	struct fb_var_screeninfo *var = &info->var;
	char option[16];
	int ret = 0;
	unsigned int data;

	TRACE;

	if (check_constraints(config)) return -1;

	if (!strcasecmp(par->display_interface->name, "VGA")) {
		sprintf(option, "%dx%d-32", config->comp_size_h, config->comp_size_v);
		pr_info("%s: option=%s\n", __func__, option);
		ret = fb_find_mode(&info->var, info, option, vga_mode, vga_size, NULL, 32);
		if (ret == 0) {
			pr_err("%s: fb_find_mode failed\n", __func__);
			return -1;
		} else if (ret >= 3) {
			pr_warn("%s: fb_find_mode using default mode: %dx%d-32\n", __func__, vga_mode[0].xres, vga_mode[0].yres);
		} else {
			pr_info("%s: fb_find_mode using mode: %s\n", __func__, option);
		}
		fb_var_to_videomode(info->mode, &info->var);
	}

	data = (var->left_margin << 16) | var->right_margin;
	writel(data, par->mmio + LCDC_H_PORCH);
	writel(var->hsync_len, par->mmio + LCDC_H_WIDTH);

	data = (var->upper_margin << 16) | var->lower_margin;
	writel(data, par->mmio + LCDC_V_PORCH);
	writel(var->vsync_len, par->mmio + LCDC_V_WIDTH);

	data = (var->yres << 16) | (var->xres);
	writel(data, par->mmio + LCDC_RGB_CROP_SIZE);
	writel(data, par->mmio + LCDC_RGB_ORI_SIZE);
	writel(data, par->mmio + LCDC_HV_SIZE);

	ui_crop(info);
	ui_format(info);
	cp_dither(info);

	npsc01_fb_clk_set(info);

	return 0;
}

static int npsc01_fb_sync_cmp(u32 a, u32 b)
{
	if (a == b)
		return 0;

	return ((s32)a - (s32)b) < 0 ? -1 : 1;
}

struct sync_pt *npsc01_fb_sync_pt_create(struct fb_sync_timeline *obj, u32 value)
{
	struct fb_sync_pt *pt;

	pt = (struct fb_sync_pt *)
		sync_pt_create(&obj->obj, sizeof(struct fb_sync_pt));

	pt->value = value;
	lcdc_dbg("pt value %d\n", pt->value);

	return (struct sync_pt *)pt;
}

static struct sync_pt *npsc01_fb_sync_pt_dup(struct sync_pt *sync_pt)
{
	struct fb_sync_pt *pt = (struct fb_sync_pt *) sync_pt;
	struct fb_sync_timeline *obj =
		(struct fb_sync_timeline *)sync_pt->parent;

	lcdc_dbg("npsc01_fb_sync_pt_dup");
	return (struct sync_pt *) npsc01_fb_sync_pt_create(obj, pt->value);
}

static int npsc01_fb_sync_pt_has_signaled(struct sync_pt *sync_pt)
{
	struct fb_sync_pt *pt = (struct fb_sync_pt *)sync_pt;
	struct fb_sync_timeline *obj =
		(struct fb_sync_timeline *)sync_pt->parent;

	return npsc01_fb_sync_cmp(obj->value, pt->value) >= 0;
}

static int npsc01_fb_sync_pt_compare(struct sync_pt *a, struct sync_pt *b)
{
	struct fb_sync_pt *pt_a = (struct fb_sync_pt *)a;
	struct fb_sync_pt *pt_b = (struct fb_sync_pt *)b;

	return npsc01_fb_sync_cmp(pt_a->value, pt_b->value);
}

static void npsc01_fb_sync_print_obj(struct seq_file *s,
		struct sync_timeline *sync_timeline)
{
	struct fb_sync_timeline *obj = (struct fb_sync_timeline *)sync_timeline;

	seq_printf(s, "%d", obj->value);
}

static void npsc01_fb_sync_print_pt(struct seq_file *s, struct sync_pt *sync_pt)
{
	struct fb_sync_pt *pt = (struct fb_sync_pt *)sync_pt;
	struct fb_sync_timeline *obj =
		(struct fb_sync_timeline *)sync_pt->parent;

	seq_printf(s, "%d / %d", pt->value, obj->value);
}

static int npsc01_fb_sync_fill_driver_data(struct sync_pt *sync_pt,
		void *data, int size)
{
	struct fb_sync_pt *pt = (struct fb_sync_pt *)sync_pt;

	if (size < sizeof(pt->value))
		return -ENOMEM;

	memcpy(data, &pt->value, sizeof(pt->value));

	return sizeof(pt->value);
}

struct sync_timeline_ops npsc01_fb_sync_timeline_ops = {
	.driver_name = "fb_sync",
	.dup = npsc01_fb_sync_pt_dup,
	.has_signaled = npsc01_fb_sync_pt_has_signaled,
	.compare = npsc01_fb_sync_pt_compare,
	.print_obj = npsc01_fb_sync_print_obj,
	.print_pt = npsc01_fb_sync_print_pt,
	.fill_driver_data = npsc01_fb_sync_fill_driver_data,
};

static struct fb_sync_timeline *npsc01_fb_sync_timeline_create(const char *name)
{
	struct fb_sync_timeline *obj = (struct fb_sync_timeline *)
		sync_timeline_create(&npsc01_fb_sync_timeline_ops,
				sizeof(struct fb_sync_timeline),
				name);

	if (obj != NULL) obj->value = 0;
	return obj;
}

static int npsc01_fb_sync_timeline_destroy(struct fb_sync_timeline *obj)
{
	sync_timeline_destroy(&obj->obj);
	return 0;
}

static void npsc01_fb_sync_timeline_signal(struct fb_sync_timeline *obj)
{
	obj->value++;

	lcdc_dbg("signal pt(s) less than %d\n", obj->value);
	sync_timeline_signal(&obj->obj);
}

static long npsc01_fb_sync_create_fence(struct fb_sync_timeline *obj, unsigned long arg)
{
	int fd = get_unused_fd();
	int err;
	struct sync_pt *pt;
	struct sync_fence *fence;
	struct fb_sync_create_fence_data data;
	u32 pt_value;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	pt_value = obj->value + data.inc;
	pt = npsc01_fb_sync_pt_create(obj, pt_value);
	if (pt == NULL) {
		err = -ENOMEM;
		goto err;
	}

	fence = sync_fence_create("fbfence", pt);
	if (fence == NULL) {
		sync_pt_free(pt);
		err = -ENOMEM;
		goto err;
	}

	data.fence = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data))) {
		sync_fence_put(fence);
		err = -EFAULT;
		goto err;
	}

	sync_fence_install(fence, fd);

	return 0;

err:
	put_unused_fd(fd);
	return err;
}

static int npsc01_fb_wait_for_vsync(struct npsc01_par* par)
{
	INIT_COMPLETION(par->vsync);
	if (wait_for_completion_timeout(&par->vsync, HZ) == 0) {
		lcdc_err("%s, can not get hw vsync interrupt.\n", __func__);
		return -ETIMEDOUT;
	}

	return 0;
}


static ssize_t npsc01_fb_offset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct npsc01_par *par = info->par;
	unsigned int hsize, vsize;
	unsigned int data;

	sscanf(buf, "%u,%u", &hsize, &vsize);

	data = (vsize<< 16) | (hsize);
	writel(data, par->mmio + LCDC_RGB_CROP_OFFSET);
	return size;
}

static ssize_t npsc01_fb_offset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct npsc01_par *par = info->par;
	unsigned int data;

	data = readl(par->mmio + LCDC_RGB_CROP_OFFSET);

	return snprintf(buf, PAGE_SIZE, "%u,%u\n",
		data&0xfff, (data>>16)&0xfff);
}

static ssize_t npsc01_fb_fullsize_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct npsc01_par *par = info->par;
	unsigned int hsize, vsize;
	unsigned int data;

	sscanf(buf, "%u,%u", &hsize, &vsize);

	data = (vsize<< 16) | (hsize);
	writel(data, par->mmio + LCDC_RGB_ORI_SIZE);
	return size;
}

static ssize_t npsc01_fb_fullsize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct fb_info *info = dev_get_drvdata(dev);
	struct npsc01_par *par = info->par;
	unsigned int data;

	data = readl(par->mmio + LCDC_RGB_ORI_SIZE);

	return snprintf(buf, PAGE_SIZE, "%u,%u\n",
		data&0xfff, (data>>16)&0xfff);
}

static ssize_t npsc01_fb_show_vsync_event(struct device *device,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	struct fb_info *info = dev_get_drvdata(device);
	struct npsc01_par *par = info->par;

	if (info->state != FBINFO_STATE_RUNNING) {
		lcdc_warn("lcdc is not running, no vsync to report!\n");
		return -ENXIO;
	}

	npsc01_fb_wait_for_vsync(par);

	ret = snprintf(buf, PAGE_SIZE, "VSYNC=%llu\n", ktime_to_ns(par->vsync_time));
	buf[strlen(buf) + 1] = '\0';
	sysfs_notify(&info->dev->kobj, NULL, "vsync_event");

	lcdc_dbg("show vsync.done = %d\n", par->vsync.done);
	lcdc_dbg("vsync_time = %llu strlen(buf) = %d\n\n",
			ktime_to_ns(par->vsync_time), strlen(buf));

	return ret;
}

static struct device_attribute dev_attrs[] = {
	__ATTR(vsync_event, S_IRUGO, npsc01_fb_show_vsync_event, NULL),
	__ATTR(full_size, S_IRUGO|S_IWUSR, npsc01_fb_fullsize_show, npsc01_fb_fullsize_store),
	__ATTR(offset, S_IRUGO|S_IWUSR, npsc01_fb_offset_show, npsc01_fb_offset_store),
};

static int npsc01_fb_create_device_files(struct fb_info *info)
{
	int i, error = 0;
	struct npsc01_par *par = info->par;

	for (i = 0; i < ARRAY_SIZE(dev_attrs); i++) {
		error = device_create_file(info->dev, &dev_attrs[i]);
		if (error) break;
	}

	if (error) {
		while (--i >= 0)
			device_remove_file(info->dev, &dev_attrs[i]);
		return error;
	}

	init_completion(&par->vsync);

	return 0;
}

static irqreturn_t __maybe_unused npsc01_fb_err_handler(int irq, void *data)
{
	struct fb_info *info = (struct fb_info*)data;
	struct npsc01_par *par = info->par;
	unsigned int int_status = readl(par->mmio + LCDC_INT_STATUS);

	writel(int_status, par->mmio + LCDC_INT_CLR);
	return IRQ_HANDLED;
}

static irqreturn_t npsc01_fb_vert_handler(int irq, void *data)
{
	struct fb_info *info = (struct fb_info*)data;
	struct npsc01_par *par = info->par;
	unsigned int int_status = readl(par->mmio + LCDC_INT_STATUS);

	writel(int_status, par->mmio + LCDC_INT_CLR);
	if (int_status & VERT_FLAG) {
		par->vsync_time = ktime_get();
		complete_all(&par->vsync);
		npsc01_fb_sync_timeline_signal(par->fb_timeline);
	}else if(int_status & OVERALL_UNDERFLOW_FLAG){
		lcdc_dbg("lcd overall underflow happens! lcd_reset 0\n");
		reg_clr_bit(par->mmio + LCDC_DSPC_CTRL, VGA_ENABLE);
		mdelay(2);
		reg_set_bit(par->mmio + LCDC_DSPC_CTRL, VGA_ENABLE);

	}
	return IRQ_HANDLED;
}

static int npsc01_fb_save(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	int i;

	for (i=0; i<REG_NUM; i++)
		par->regsave[i] = ((unsigned int *)(par->mmio))[i];

	/* all INT_CLR = 1 */
	par->regsave[0x5] = 0xffffffff;

	return 0;
}

static int npsc01_fb_restore(struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	int i;

	for (i=REG_NUM-1; i>=0; i--)
		((unsigned int *)(par->mmio))[i] = par->regsave[i];

	return 0;
}

static int npsc01_fb_ioctl(struct fb_info *info, unsigned int cmd,
			unsigned long arg)
{
	struct npsc01_par *par = info->par;
	unsigned int tmp[2] = {0, 0};
	int ret = 0;

	switch (cmd) {
		case FBIO_WAITFORVSYNC:
			ret = npsc01_fb_wait_for_vsync(par);
			if (ret) return ret;
			break;

		case FB_GET_SRC_HEIGHT:
			if (copy_to_user((void*)arg, &info->mode->yres,
						sizeof(unsigned int)))
				return -EFAULT;
			break;

		case FB_CREATE_VIDEO_FENCE:
			ret = (int)npsc01_fb_sync_create_fence(par->fb_timeline, arg);
			if (ret) return ret;
			break;

		case FB_LCDC_SET_CONFIG:
			if (!copy_from_user(&par->config, (void*)arg, sizeof(struct npsc01_lcdc_config)))
				ret = set_config(info, &par->config);
			break;

		case FB_LCDC_DISABLE:
			npsc01_lcdc_disable(info);
			break;

		case FB_LCDC_ENABLE:
			npsc01_lcdc_enable(info);
			break;

		case FB_LCDC_GET_REG:
			if (!copy_from_user(&tmp[0], (void*)arg, sizeof(int))) {
				tmp[1] = readl(par->mmio + tmp[0]);
				if (copy_to_user((void*)arg, &tmp[1], sizeof(int)))
					lcdc_err("%s: copy_to_user failed\n", __func__);
			}
			break;

		default:
			lcdc_err("%s: No such IOCTL: 0x%x\n", __func__, cmd);
			ret = -1;
			break;
	}

	return ret;
}

static void npsc01_fb_select_ui_ptr(struct fb_info *info, int id)
{
	struct npsc01_par *par = info->par;
	unsigned int data = readl(par->mmio + LCDC_RD_PTRSEL);
	data &= ~RGB_PTRSEL_MASK;
	data |= id;
	writel(data, par->mmio + LCDC_RD_PTRSEL);
}

static int npsc01_fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct npsc01_par *par = info->par;
	unsigned int buffer_id = var->yoffset / var->yres;

	if (info->state != FBINFO_STATE_RUNNING)
		return -ENXIO;

	if (var->yoffset > var->yres_virtual)
		return -EINVAL;

	buffer_id = (buffer_id >= par->nr_buffers)?par->nr_buffers:buffer_id;
	npsc01_fb_select_ui_ptr(info, buffer_id);
	npsc01_fb_wait_for_vsync(par);

	return 0;
}

static int npsc01_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct npsc01_par *par = info->par;

	var->xres_virtual	= var->xres;
	var->yres_virtual	= par->nr_buffers * var->yres;

	/* set bitfield */
	memset(&var->transp, 0, sizeof(var->transp));

	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;

	switch (var->bits_per_pixel) {
	case 15:
		var->red.length = 5;
		var->blue.length = 5;
		var->green.length = 5;
		break;
	case 16:
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		break;
	case 24:
	case 32:
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->bits_per_pixel = 32;
		break;
	default:
		var->red.length = 5;
		var->green.length = 6;
		var->blue.length = 5;
		var->bits_per_pixel = 16;
		break;
	}

	var->blue.offset = 0;
	var->green.offset = var->blue.offset + var->blue.length;
	var->red.offset = var->green.offset + var->green.length;

	return 0;
}

static int npsc01_fb_blank(int blank, struct fb_info *info)
{
	lcdc_dbg("%s: mode=%x\n", __func__, blank);

	switch (blank) {
		case FB_BLANK_NORMAL:
		case FB_BLANK_POWERDOWN:
		case FB_BLANK_VSYNC_SUSPEND:
		case FB_BLANK_HSYNC_SUSPEND:
			npsc01_lcdc_disable(info);
			break;
		case FB_BLANK_UNBLANK:
			npsc01_lcdc_enable(info);
			break;
	}
	return 0;
}

static int npsc01_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long physical = info->fix.smem_start >> PAGE_SHIFT;
	unsigned long vsize = vma->vm_end - vma->vm_start;

	lcdc_dbg("%s size=%lx\n", __func__, vsize);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	if (remap_pfn_range(vma, vma->vm_start, physical,
				vsize, vma->vm_page_prot))
		return -EAGAIN;
	return 0;
}

static struct fb_ops npsc01_fb_ops = {
	.fb_ioctl		= npsc01_fb_ioctl,
	.fb_pan_display	= npsc01_fb_pan_display,
	.fb_check_var	= npsc01_fb_check_var,
	.fb_blank		= npsc01_fb_blank,
	.fb_mmap		= npsc01_fb_mmap,
};

static int npsc01_fb_parse_panel_power_func(struct platform_device *pdev)
{
	int ret;
	const char *panel;
	char  xxxx_power_init[64];
	char  xxxx_power_on[64];
	char  xxxx_power_off[64];

	struct fb_info *info = NULL;
	struct npsc01_par *par = NULL;

	info = platform_get_drvdata(pdev);
	par = info->par;

	of_property_read_string(par->display_interface, "panel", &(par->panel));

	if(!strcmp(par->display_interface->name, "VGA") ||
			!strcmp(par->display_interface->name, "vga"))
		panel = "vga";
	else
		panel = par->panel;

	sprintf(xxxx_power_init, "%s_power_init", panel);
	sprintf(xxxx_power_on, "%s_power_on", panel);
	sprintf(xxxx_power_off, "%s_power_off", panel);

	panel_power_init = (void*)kallsyms_lookup_name(xxxx_power_init);
	if (NULL == panel_power_init) {
		dev_err(&pdev->dev, "Can't find %s function.\n", xxxx_power_init);
		return -ENOENT;
	}

	panel_power_on = (void*)kallsyms_lookup_name(xxxx_power_on);
	if (NULL == panel_power_on) {
		dev_err(&pdev->dev, "Can't find %s function.\n", xxxx_power_on);
		return -ENOENT;
	}

	panel_power_off = (void*)kallsyms_lookup_name(xxxx_power_off);
	if (NULL == panel_power_off) {
		dev_err(&pdev->dev, "Can't find %s function.\n", xxxx_power_off);
		return -ENOENT;
	}

	ret = panel_power_init();
	if (ret)
		dev_err(&pdev->dev, "panel_power_init error.\n");

#if 1 //DISPLAY_DRIVER_INIT_BOOT_ON
	ret = panel_power_on();
	if (ret)
		dev_err(&pdev->dev, "panel_power_on error.\n");
#endif

	return 0;
}

static int npsc01_fb_parse_display_interface(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct npsc01_par *par = info->par;

	par->display_interface = of_parse_phandle(pdev->dev.of_node, "display_interface", 0);
	if (!par->display_interface) {
		dev_err(&pdev->dev, "Failed to get display_interface for %s.\n",
				pdev->dev.of_node->full_name);
		return -ENODEV;
	}
	dev_warn(&pdev->dev, "display_interface->name = %s\n", par->display_interface->name);

	return 0;
}

static const struct pixclk_pll_mapping pixclock_mappings[] = {
	/* pixclock    pll_rate    divider */

	/* 480x800@60 */
	{40675, 49000000, 2},
	/* 640x480@60 */
	{39721, 50000000, 2},
	/* 800x600@60 */
	{25000, 80000000, 2},
	/* 960x540@60 */
	{23721, 84300000, 2},
    /* 1024x600@60 */
	{20833, 384000000, 8},
    /* 800x480@60 raymond add*/
	{31250, 384000000, 12},
    /* 800x480@60 raymond add*/
	{36458, 384000000, 14},
    /* 800x480@60 raymond add*/
	{23437, 384000000, 9},
	/* 1024x600@60 */
	{19841, 100800000, 2},
	/* 1024x768@60 */
	{15384, 130000000, 2},
	/* 1280x720@60 */
	{15625, 965250000, 13},
	/* 1280x720@50 */
	{13468, 965250000, 13},
	/* 720x1280@60 */
	{15723, 127200000, 2},
	/* 1280x800@60 */
	{12048, 996000000, 12},
	/* 1366x768@60 */
	{13806, 144800000, 2},
	/* 1400x1050@60 */
	{9259, 216000000, 2},
	/* 1680x1050@60 */
	{6848, 292000000, 2},
	/* 1600x1200@60 */
	{6172, 324000000, 2},
	/* 1920x1080@60 */
	{6734, 1188000000, 8},
	/* 1920x1200@60 */
	{5177, 386000000, 2},
	/* 2560x1600@60 */
	{3724, 537000000, 2},
};

static int npsc01_fb_clk_set(struct fb_info *info)
{

#if FPGA_VERIFY

	/* Use the clks supplied by FPGA */
#else
	struct npsc01_par *par = info->par;
	unsigned long i, pll_rate, divider;

	for (i = 0; i < ARRAY_SIZE(pixclock_mappings); i++) {
		if (pixclock_mappings[i].pixclock == info->mode->pixclock) {
			pll_rate = pixclock_mappings[i].pll_rate;
			divider = pixclock_mappings[i].divider;
			break;
		}
	}

	if (i == ARRAY_SIZE(pixclock_mappings)) {
		lcdc_err("Failed to match %d in pixclock_mappings.\n", info->mode->pixclock);
		return -EINVAL;
	}
	if (clk_set_rate(par->pxlclk,pll_rate / divider)) {
		lcdc_err("Failed to set pxlclk rate.\n");
		return -EINVAL;
	}
#endif
	return 0;
}

static int npsc01_fb_clk_enable_disable(struct fb_info *info, bool enable)
{
#if FPGA_VERIFY

	/* Use the clks supplied by FPGA */
#else
	struct npsc01_par *par = info->par;

	if (enable) {
		clk_prepare_enable(par->pclk);
		clk_prepare_enable(par->pxlclk);
	} else {
		clk_disable_unprepare(par->pxlclk);
		clk_disable_unprepare(par->pclk);
	}
#endif

	return 0;
}

static int npsc01_fb_clk_init(struct platform_device *pdev)
{
#if FPGA_VERIFY


	/* Use the clks supplied by FPGA */
#else
	struct fb_info *info = platform_get_drvdata(pdev);
	struct npsc01_par *par = info->par;

#ifdef CONFIG_RESET_CONTROLLER
/*	par->reset = devm_reset_control_get(&pdev->dev, "lcdc_reset");
	if (IS_ERR(par->reset)) {
		dev_err(&pdev->dev, "%s: lcdc reset get failed\n", __func__);
		return -ENXIO;
	}
*/
#endif

	of_property_read_string_index(pdev->dev.of_node, "clock-names", 0, &par->pxlclk_name);
//	par->pxlclk = devm_clk_get(&pdev->dev, par->pxlclk_name);
	par->pxlclk = clk_get(&pdev->dev, par->pxlclk_name);

	of_property_read_string_index(pdev->dev.of_node, "clock-names", 1, &par->pclk_name);
	par->pclk = clk_get(&pdev->dev, par->pclk_name);

	if (IS_ERR(par->pxlclk) || IS_ERR(par->pclk)) {
		dev_err(&pdev->dev, "%s: lcdc clk get failed\n", __func__);
		return -ENXIO;
	}
#endif
	return 0;
}

static int npsc01_fb_pinctrl_init(struct platform_device *pdev)
{
#ifdef CONFIG_PINCTRL
//	struct fb_info *info = platform_get_drvdata(pdev);
//	struct npsc01_par *par = info->par;

//	if (strcasecmp(par->display_interface->name, "VGA"))
//	pinctrl_put(pdev->dev.pins->p);

	pdev->dev.pins->p = pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pdev->dev.pins->p)) {
		pr_err("Fail to get lcd0 pinctrl\n");
		return -1;
	}


#endif

	return 0;
}

static int npsc01_fb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct npsc01_par *par;
	struct resource *res;
	int ret = 0;
	dma_addr_t phy_addr;
	int i;

	info = framebuffer_alloc(sizeof(struct npsc01_par), &pdev->dev);
	if (!info) {
		dev_err(&pdev->dev, "%s: framebuffer_alloc failed\n", __func__);
		ret = -ENOMEM;
		goto error_framebuffer_alloc;
	}
	platform_set_drvdata(pdev, info);
	par = info->par;
	par->nr_buffers = 3;

	/* Parse FDT */
	ret = npsc01_fb_parse_display_interface(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s: Failed to parse lcdc display interface.\n", __func__);
		goto error_dts;
	}

	ret = npsc01_fb_parse_panel_power_func(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse display power function.\n");
		goto error_dts;
	}

	/* Registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "%s: get mem resource 0 failed\n", __func__);
		ret = -EINVAL;
		goto error_ioremap;
	}

	par->mmio = ioremap_nocache(res->start, resource_size(res));
	dev_info(&pdev->dev, "mmio = 0x%08x\n", (unsigned int)par->mmio);

	info->fbops = &npsc01_fb_ops;
	set_options(info);
	npsc01_fb_check_var(&info->var, info);

	info->fix.smem_len = info->fix.line_length * info->var.yres;
	info->fix.smem_len *= par->nr_buffers;
	info->fix.smem_len += 1024; /* padding 1 KB */
	info->fix.smem_len = PAGE_ALIGN(info->fix.smem_len);

	/* alloc FB memory */
	info->screen_base = dma_alloc_coherent(&pdev->dev,
			info->fix.smem_len, &phy_addr, GFP_KERNEL);
	if (!info->screen_base) {
		dev_err(&pdev->dev, "%s: alloc frame buffer fail\n", __func__);
		ret = -ENOMEM;
		goto error_ioremap;
	}
	info->fix.smem_start = phy_addr;
	memset(info->screen_base, 0x0, info->fix.smem_len);

	for (i=0; i<par->nr_buffers; i++) {
		writel(info->fix.smem_start + (i * info->fix.line_length * info->var.yres),
				par->mmio + LCDC_RGB_PTR0 + i*4);
		lcdc_info("LCDC_RGB_PTR%d: 0x%08x\n", i, readl(par->mmio + LCDC_RGB_PTR0 + i*4));
	}

	if (register_framebuffer(info) < 0) {
		dev_err(&pdev->dev, "%s: register framebuffer error\n", __func__);
		goto error_register_framebuffer;
	}

	ret = npsc01_fb_notifier_register(info);
	if(ret) {
		lcdc_err("Failed to register lcd notifier client.\n");
		goto error_register_framebuffer;
	}

	/* Device files */
	ret = npsc01_fb_create_device_files(info);
	if (ret) {
		lcdc_err("Failed to create npsc01 lcdfb device files.\n");
		goto error_register_framebuffer;
	}

	/* Timeline */
	par->fb_timeline = npsc01_fb_sync_timeline_create("fb-timeline");
	if (!par->fb_timeline) {
		lcdc_err("Failed to create npsc01 lcdfb sync timeline.\n");
		goto error_register_framebuffer;
	}

	/* Interrupts */
	//par->err_irq = platform_get_irq(pdev, 0);
	//par->vert_irq = platform_get_irq(pdev, 1);
	//par->horiz_irq = platform_get_irq(pdev, 2);
	//if (par->err_irq == -ENXIO || par->vert_irq == -ENXIO || par->horiz_irq == -ENXIO) {
	
	par->irq = platform_get_irq(pdev, 0);
	if (par->irq == -ENXIO) {
		dev_err(&pdev->dev, "%s: get irq failed\n", __func__);
		ret = -ENXIO;
		goto error_request_irq;
	}
/*
	ret = devm_request_irq(&pdev->dev, par->err_irq, npsc01_fb_err_handler,
			IRQF_TRIGGER_HIGH, NS_FB_NAME, info);
	if (ret) {
		dev_err(&pdev->dev, "%s: request err_irq failed\n", __func__);
		goto error_request_irq;
	}
*/
	ret = devm_request_irq(&pdev->dev, par->irq, npsc01_fb_vert_handler,
			IRQF_TRIGGER_HIGH, NS_FB_NAME, info);
	if (ret) {
		dev_err(&pdev->dev, "%s: request irq failed\n", __func__);
		goto error_request_irq;
	}

	if (npsc01_fb_clk_init(pdev)) {
		dev_err(&pdev->dev, "%s: clock init failed\n", __func__);
		goto error_clock_init;
	}

//	npsc01_fb_pinctrl_init(pdev);
	npsc01_fb_save(info);
	TRACE;

#if 1 //DISPLAY_DRIVER_INIT_BOOT_ON
	npsc01_lcdc_disable(info);
	if (npsc01_fb_clk_set(info)) {
		dev_err(&pdev->dev, "%s: clock set failed\n", __func__);
		goto error_clock_set;
	}

	npsc01_fb_clk_enable_disable(info, true);
	set_config(info, &par->config);
	npsc01_lcdc_enable(info);
#endif

	device_enable_async_suspend(&pdev->dev);

	par->lcdc_enabled = 1;
	return 0;

error_clock_set:
error_clock_init:
	unregister_framebuffer(info);
	npsc01_fb_sync_timeline_destroy(par->fb_timeline);
error_register_framebuffer:
error_request_irq:
	dma_free_coherent(&pdev->dev, info->fix.smem_len,
		info->screen_base, phy_addr);
	iounmap(par->mmio);
error_ioremap:
error_dts:
	framebuffer_release(info);
error_framebuffer_alloc:
	return ret;
}

static int npsc01_fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = platform_get_drvdata(pdev);
	struct npsc01_par *par;

	if (info) {
		unregister_framebuffer(info);
		par = info->par;
		iounmap(par->mmio);
		framebuffer_release(info);
	}

	return 0;
}

static const struct of_device_id npsc01_fb_of_match[] = {
	{ .compatible = "nufront,nusmartfb", },
	{},
};
MODULE_DEVICE_TABLE(of, npsc01_fb_of_match);

static struct platform_driver npsc01_fb_driver = {
	.probe		= npsc01_fb_probe,
	.remove		= npsc01_fb_remove,
	.suspend	= npsc01_fb_plat_suspend,
	.resume		= npsc01_fb_plat_resume,
	.driver		= {
		.name = NS_FB_NAME,
		.of_match_table	= of_match_ptr(npsc01_fb_of_match),
	},
};

static int __init npsc01_fb_init(void)
{
	int ret;

	TRACE;

	ret = platform_driver_register(&npsc01_fb_driver);
	if (ret) lcdc_err("%s: npsc01_fb_driver register failed, ret = %d\n", __func__, ret);

	return ret;
}

static void __exit npsc01_fb_exit(void)
{
	platform_driver_unregister(&npsc01_fb_driver);
}

module_init(npsc01_fb_init);
module_exit(npsc01_fb_exit);
