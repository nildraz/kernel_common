#include <linux/fb.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <video/npsc01-lcdc.h>
#include "npsc01_panel.h"
extern void AVI_LCD_Backlight_Power_Set(int OnOff);
extern void AVI_S2_PowerSaving_Set(int OnOff);

#define LCD_BL_EN 55

//static struct regulator *rgl_bl, *rgl_2v8;

#define RES_1024_600  1
#define RES_800_480   2

#ifdef CONFIG_TOUCHSCREEN_FT5X0X
    //with enable CONFIG_TOUCHSCREEN_FT5X0X in kernel 
    #define LCD_RES   RES_1024_600 
#else
    //with enable CONFIG_TOUCHSCREEN_GT9XX in kernel
    //#define KLCDClk_32
    //#define KLCDClk_27
    #define KLCDClk_42
    //#define LCD_RES   RES_800_480 
    #define LCD_RES   RES_1024_600  
#endif


#if(LCD_RES == RES_1024_600)
//declare_panel_mode(npsc01_lvds) = {
struct fb_videomode npsc01_lvds_mode = { 
	.name = to_string(npsc01_lvds),
	.refresh = 60,
	.xres = 1024,
	.yres = 600,
	.pixclock = 20833,
	.left_margin = 220,
	.right_margin = 90,
	.upper_margin = 25,
	.lower_margin = 5,
	.hsync_len = 10,
	.vsync_len = 5,
	.sync = 0,
	.vmode = 0,
	.flag = 0,
};

#else

//for Panel 5inch   800x480 .left_margin = 220,
//for Panel 2.8inch 240x320 .left_margin = 2,
#ifdef KLCDClk_32
struct fb_videomode npsc01_lvds_mode = { 
	.name = to_string(npsc01_lvds),
	.refresh = 60,
	.xres = 800,
	.yres = 480,
	.pixclock = 31250,
	.left_margin = 220,
	.right_margin = 90,
	.upper_margin = 25,
	.lower_margin = 5,
	.hsync_len = 10,
	.vsync_len = 5,
	.sync = 0,
	.vmode = 0,
	.flag = 0,
};
#endif

#ifdef KLCDClk_27
struct fb_videomode npsc01_lvds_mode = { 
	.name = to_string(npsc01_lvds),
	.refresh = 60,
	.xres = 800,
	.yres = 480,
	.pixclock = 36458,
	.left_margin = 220,
	.right_margin = 90,
	.upper_margin = 25,
	.lower_margin = 5,
	.hsync_len = 10,
	.vsync_len = 5,
	.sync = 0,
	.vmode = 0,
	.flag = 0,
};
#endif

#ifdef KLCDClk_42
struct fb_videomode npsc01_lvds_mode = { 
	.name = "npsc01_lvds",
	.refresh = 60,
	.xres = 800,
	.yres = 480,
	.pixclock = 23437,
	.left_margin = 220,
	.right_margin = 90,
	.upper_margin = 25,
	.lower_margin = 5,
	.hsync_len = 10,
	.vsync_len = 5,
	.sync = 0,
	.vmode = 0,
	.flag = 0,
};
#endif

#endif

declare_panel_power_on(npsc01_lvds)
{
	int ret = 0;
	pr_info("%s\n", __func__);

//	ret = regulator_enable(rgl_2v8);
	

	return ret;
}

declare_panel_power_off(npsc01_lvds)
{
	int ret = 0;
	pr_info("%s\n", __func__);

//	msleep(120);
//	ret = regulator_disable(rgl_2v8);

	return ret;
}

declare_panel_backlight_on(npsc01_lvds)
{
	int ret = 0;
	pr_info("%s\n", __func__);
//	ret = regulator_enable(rgl_bl);
//	gpio_direction_output(LCD_BL_EN, 1);

    //Raymond 20190710: power on in kernel , power off in scan service
    //otherwise touch driver will has problem.   
    AVI_S2_PowerSaving_Set(1); //scan service will do AVI_S2_PowerSaving_Set
    msleep(200); 
    AVI_LCD_Backlight_Power_Set(1);  
 
	return ret;
}

declare_panel_backlight_off(npsc01_lvds)
{
	int ret = 0;
	pr_info("%s\n", __func__);
//	ret = regulator_disable(rgl_bl);
//	gpio_direction_output(LCD_BL_EN, 0);

    AVI_LCD_Backlight_Power_Set(0);
    //AVI_S2_PowerSaving_Set(0); //scan service will do AVI_S2_PowerSaving_Set
	return ret;
}

declare_panel_power_init(npsc01_lvds)
{
//	int status = 0;
	pr_info("%s\n", __func__);
/*
	status = gpio_request(LCD_RST_N, "LCD_RST_N");
	if(status) {
		pr_err("Failed to request gpio %d.\n", LCD_RST_N);
		return -1;
	}
*/
	return 0;
}


