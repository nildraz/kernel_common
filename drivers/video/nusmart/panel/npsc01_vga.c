#include <linux/fb.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <video/npsc01-lcdc.h>
#include "npsc01_panel.h"

static struct regulator *rgl_vga_5v;

struct fb_videomode vga_mode[] = {

	/* Standard modes */

	/* 640x480 @ 60 Hz, 31.5 kHz hsync */
	{ NULL, 60, 640, 480, 39721, 40, 24, 32, 11, 6, 2,	0,
		FB_VMODE_NONINTERLACED },

	/* 800x600 @ 60 Hz, 37.8 kHz hsync */
	{ NULL, 60, 800, 600, 25000, 80, 48, 11, 3, 32, 4, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1024x600 @ 30 Hz, 37.2 kHz hsync */
	{ NULL, 30, 1024, 600, 41612, 170, 68, 10, 5, 30, 5, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1024x600 @ 60 Hz, 37.2 kHz hsync */
	{ NULL, 60, 1024, 600, 20806, 170, 68, 10, 5, 30, 5, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1024x768 @ 60 Hz, 48.4 kHz hsync */
	{ NULL, 60, 1024, 768, 15384, 168, 8, 29, 3, 144, 6, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1280x720@60Hz, 44.444 kHz hsync, WXGA 16:9 aspect ratio */
	{ NULL, 60, 1280, 720, 15625, 80, 48, 13, 3, 32, 5, 0,
		FB_VMODE_NONINTERLACED },

	/* 1280x720@50Hz, 44.444 kHz hsync, WXGA 16:9 aspect ratio */
	{ NULL, 50, 1280, 720, 13468, 220, 110, 20, 5, 40, 5, 0,
		FB_VMODE_NONINTERLACED },

	/* 1280x800, 60 Hz, 47.403 kHz hsync, WXGA 16:10 aspect ratio */
	{ NULL, 60, 1280, 800, 12048, 200, 64, 24, 1, 136, 3, 0,
		FB_VMODE_NONINTERLACED },

	/* 1366x768, 60 Hz, 47.403 kHz hsync, WXGA 16:9 aspect ratio */
	{ NULL, 60, 1366, 768, 13806, 120, 10, 14, 3, 32, 5, 0,
		FB_VMODE_NONINTERLACED },

	/* 1400x1050@60Hz, 63.9 kHz hsync */
	{ NULL, 60, 1400, 1050, 9259, 136, 40, 13, 1, 112, 3, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1600x1200@60Hz, 75.00 kHz hsync */
	{ NULL, 60, 1600, 1200, 6172, 304, 64, 46, 1, 192, 3, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1680x1050@60Hz, 65.191 kHz hsync */
	{ NULL, 60, 1680, 1050, 6848, 280, 104, 30, 3, 176, 6, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1920x1200@60 Hz, 74.5 Khz hsync */
	{ NULL, 60, 1920, 1200, 5177, 128, 336, 1, 38, 208, 3, 0,
	  FB_VMODE_NONINTERLACED },

	/* 1920x1080@60Hz, 67.5 Khz hsync */
	{ NULL, 60, 1920, 1080, 6734, 148, 88, 36, 4, 44, 5, 0,
		FB_VMODE_NONINTERLACED },

	/* 2560x1600@60 Hz, 98.713 Khz hsync, WQXGA 16:10 aspect ratio */
	{ NULL, 60, 2560, 1600, 3724, 80, 48, 37, 3, 32, 6, 0,
	  FB_VMODE_NONINTERLACED },

	{ NULL, 60, 960, 540, 23721, 142, 98, 15, 7, 48, 1, 0,
	  FB_VMODE_NONINTERLACED },

	{ NULL, 60, 1024, 600, 19841, 240, 48, 12, 3, 32, 10, 0,
	  FB_VMODE_NONINTERLACED },
};
struct fb_videomode current_mode;
unsigned int vga_size = ARRAY_SIZE(vga_mode);

declare_panel_power_on(vga)
{
	int ret = 0;
	pr_info("%s\n", __func__);
	ret = regulator_enable(rgl_vga_5v);
	return ret;
}

declare_panel_power_off(vga)
{
	int ret = 0;
	pr_info("%s\n", __func__);
	ret = regulator_disable(rgl_vga_5v);
	return ret;
}

declare_panel_power_init(vga)
{
	pr_info("%s\n", __func__);
	rgl_vga_5v = regulator_get(NULL, "VDD_5V");
	if (IS_ERR(rgl_vga_5v)) {
		pr_err("Failed to get VDD_5V regulator.\n");
		return -1;
	}

#if !DISPLAY_DRIVER_INIT_BOOT_ON
	if(regulator_enable(rgl_vga_5v)) {
		pr_err("Enable rgl_vga_5v error.\n");
		return -1;
	}
#endif
	return 0;
}
