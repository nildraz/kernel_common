/*
 * drivers/video/nusmart/panel/panel.h
 *
 * Copyright (C) 2015 Nufront Corporation
 *
 * This file should be included in each *.c file
 * under the "drivers/video/nusmart/panel/" path.
 */

#define DISPLAY_DRIVER_INIT_BOOT_ON 1

#define to_string(str)	#str
extern struct fb_videomode vga_mode[];
extern struct fb_videomode current_mode;
extern unsigned int vga_size;
extern struct fb_videomode npsc01_lvds_mode;


extern void __iomem  *prcm_base; 
extern void __iomem  *lvds_base;
extern void __iomem  *scm_base;

/* Each panel source file should implement the declare_panel_mode(panel),
 * and it is called in both nu7t_lcdfb.c and nu7t_dsi.c
 */
#define declare_panel_mode(panel) struct fb_videomode panel##_mode

/* Each panel source file should implement the following macro definitions,
 * and they are called in nu7t_lcdfb.c
 */
#define declare_panel_power_init(panel) int panel##_power_init(void)
#define declare_panel_power_on(panel) int panel##_power_on(void)
#define declare_panel_power_off(panel) int panel##_power_off(void)

/* For MIPI DSI I/F panel, following macro definitions should be implemented,
 * and they are called in nu7t_dsi.c
 */
#define declare_panel_attr(panel) struct panel_attr panel##_attr
#define declare_panel_codes_init(panel) int panel##_codes_init(unsigned int id)

/* Each panel source file should implement the following macro definitions,
 * and they are called in nu7t_bl.c
 */
#define declare_panel_backlight_on(panel) int panel##_backlight_on(void)
#define declare_panel_backlight_off(panel) int panel##_backlight_off(void)
