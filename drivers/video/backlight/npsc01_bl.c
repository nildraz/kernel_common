/*
 * Backlight driver for NPSC01 devices.
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

#include <linux/fb.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "../nusmart/panel/npsc01_panel.h"

#define DRIVER_NAME "npsc01-bl"

/*pwm0 register conf
 * BIT21-31: 	Reserved
 * BIT20:	pwm0 enable
 * BIT18-19: 	Reserved
 * BIT17:	Pwm0 polarity
 * BIT16:	Pwm0 fast mode
 * BIT15:	Pwm0 freq enable
 * BIT8-14:	Pwm0 freq value
 * BIT6-7:	Reserved
 * BIT5:	Pwm0 duty enable
 * BIT0-4:	Pwm0 duty,MAX 30, 100%*duty[4:0]/30
 */

#define PWM_EN		(0x1 << 20)
#define PWM_POL_LOW	(0x0 << 17)
#define PWM_POL_HIGH	(0x1 << 17)

#define PWM_LS_MODE	(0x0 << 16)
#define PWM_FREQ_1K	(0x4 << 8)
#define PWM_FREQ_2K	(0x2 << 8)
#define PWM_FREQ_4K	(0x1 << 8)

#define PWM_HS_MODE	(0x1 << 16)
#define PWM_FREQ_83P33K (0x8 << 8)   //avi spec 5K ~ 100K, 83.33K
#define PWM_FREQ_93P75K (0x7 << 8)   //avi spec 5K ~ 100K, 93.75K
#define PWM_FREQ_100K	(0x4 << 8)
#define PWM_FREQ_200K	(0x2 << 8)
#define PWM_FREQ_400K	(0x1 << 8)

#define PWM_FREQ_EN	(0x1 << 15)
#define PWM_DUTY_EN	(0x1 << 5)

#define PWM_DUTY(duty)	((3 * (duty)) & 0x1F)

struct backlight_data {

	const char *panel;
	u32 bl_enabled:1;

	int current_power;
	int current_brightness;

	void __iomem *pwm_base;

	struct clk *pwm_clk;
	const char *pwm_clk_name;

//	struct clk *pwm_pclk;
	const char *pwm_pclk_name;

	struct platform_device *pdev;
	struct notifier_block bl_notif;

}*bl_data;

static int (*panel_backlight_on) (void);
static int (*panel_backlight_off) (void);

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

static int npsc01bl_get_intensity(struct backlight_device *bl_dev)
{
	return bl_data->current_brightness;
}

static void npsc01bl_set_brightness(int new)
{
	u32 wdata, rdata;

	//wdata = PWM_EN |/* PWM_POL_HIGH |*/ PWM_HS_MODE | PWM_FREQ_200K | PWM_FREQ_EN | PWM_DUTY_EN | PWM_DUTY(new);
    wdata = PWM_EN | PWM_POL_LOW | PWM_HS_MODE | PWM_FREQ_93P75K /*PWM_FREQ_1K*/ | PWM_FREQ_EN | PWM_DUTY_EN | PWM_DUTY(new);


	writel(wdata, bl_data->pwm_base);
	rdata = readl(bl_data->pwm_base);
	WARN_ON((rdata & 0x1F) != PWM_DUTY(new));

	bl_data->current_brightness = new;
}

static void npsc01bl_pwm_clk_io_enable(void)
{
#ifdef CONFIG_PINCTRL
	bl_data->pdev->dev.pins->p = pinctrl_get_select_default(&bl_data->pdev->dev);
	if (IS_ERR(bl_data->pdev->dev.pins->p)) {
		pr_err("Fail to get pwm0 pinctrl\n");
		return;
	}
#endif

	clk_prepare_enable(bl_data->pwm_clk);
//	clk_prepare_enable(bl_data->pwm_pclk);
	reg_set_bit(bl_data->pwm_base, PWM_EN);
}

static void npsc01bl_pwm_clk_io_disable(void)
{
	reg_clr_bit(bl_data->pwm_base, PWM_EN);

	clk_disable_unprepare(bl_data->pwm_clk);
//	clk_disable_unprepare(bl_data->pwm_pclk);
#ifdef CONFIG_PINCTRL
	pinctrl_put(bl_data->pdev->dev.pins->p);
#endif
}

static int npsc01bl_set_intensity(struct backlight_device *bl_dev)
{
	int power, state, brightness;
	struct backlight_data *bl_data = bl_get_data(bl_dev);

	power = bl_dev->props.power;
	state = bl_dev->props.state;
	brightness = bl_dev->props.brightness;

	if (power == FB_BLANK_POWERDOWN)
		brightness = 0;

	if (state & (BL_CORE_FBBLANK || BL_CORE_SUSPENDED))
		brightness = 0;

	if (bl_data->current_brightness != brightness)
		npsc01bl_set_brightness(brightness);

	return 0;
}

static  struct backlight_ops bl_ops = {
	.get_brightness = npsc01bl_get_intensity,
	.update_status  = npsc01bl_set_intensity,
};

static int npsc01bl_suspend(void)
{
	int ret;
	if(!bl_data->bl_enabled) {
		pr_warn("bl_enabled:%d suspend is ignored.\n", bl_data->bl_enabled);
		return 0;
	}
	else
		bl_data->bl_enabled = 0;

	ret = panel_backlight_off();
	if(ret)
		pr_err("panel_backlight_off error.\n");

	npsc01bl_pwm_clk_io_disable();

	return 0;
}

static int npsc01bl_resume(void)
{
	int ret;

	if(bl_data->bl_enabled) {
		pr_warn("bl_enabled:%d resume is ignored.\n", bl_data->bl_enabled);
		return 0;
	}
	else
		bl_data->bl_enabled = 1;

	npsc01bl_pwm_clk_io_enable();
	ret = panel_backlight_on();
	if(ret)
		pr_err("panel_backlight_on error.\n");
	npsc01bl_set_brightness(bl_data->current_brightness);

	return 0;
}

static int npsc01bl_notifier_callback(struct notifier_block *nb,
		unsigned long event, void *data)
{
	switch(event) {
		
	case FB_EVENT_SUSPEND:
		npsc01bl_suspend();
		break;
		
	case FB_EVENT_RESUME:
		npsc01bl_resume();
		break;

	case FB_EVENT_IDLE_IN:
		npsc01bl_suspend();
		break;

	case FB_EVENT_IDLE_OUT:
		npsc01bl_resume();
		break;
	}
	return 0;
}

static int npsc01bl_notifier_register(void)
{
	memset(&bl_data->bl_notif, 0, sizeof(bl_data->bl_notif));
	bl_data->bl_notif.notifier_call = npsc01bl_notifier_callback;
	bl_data->bl_notif.priority = 1;

	return fb_register_client(&bl_data->bl_notif);
}

static int npsc01bl_parse_backlight_power_func(struct platform_device *pdev)
{
	char  xxxx_backlight_on[64];
	char  xxxx_backlight_off[64];

	sprintf(xxxx_backlight_on, "%s_backlight_on", bl_data->panel);
	sprintf(xxxx_backlight_off, "%s_backlight_off", bl_data->panel);

	panel_backlight_on = (void*)kallsyms_lookup_name(xxxx_backlight_on);
	if (NULL == panel_backlight_on) {
		dev_err(&pdev->dev, "Can't find %s function.\n", xxxx_backlight_on);
		return -ENOENT;
	}

	panel_backlight_off = (void*)kallsyms_lookup_name(xxxx_backlight_off);
	if (NULL == panel_backlight_off) {
		dev_err(&pdev->dev, "Can't find %s function.\n", xxxx_backlight_off);
		return -ENOENT;
	}

	return 0;
}

static int npsc01bl_parse_display_interface(struct device_node *bl_node)
{
	struct device_node *display_interface;

	display_interface = of_parse_phandle(bl_node, "display_interface", 0);
	if (!display_interface) {
		pr_err("Failed to get display_interface for %s.\n", bl_node->full_name);
		return -ENODEV;
	}

	if(!strcmp(display_interface->name, "VGA") || !strcmp(display_interface->name, "vga")) {
		pr_warn("VGA display interface doesn't need backlight driver.\n");
		return -EINVAL;
	}

	of_property_read_string(display_interface, "panel", &(bl_data->panel));
	of_property_read_string_index(bl_node, "clock-names", 0, &bl_data->pwm_clk_name);
//	of_property_read_string_index(bl_node, "clock-names", 1, &bl_data->pwm_pclk_name);

	return 0;
}

static int npsc01bl_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res_mem;
	struct backlight_device *bl_dev;
	struct backlight_properties bl_props;

	bl_data = kzalloc(sizeof(struct backlight_data), GFP_KERNEL);
	if (unlikely(!bl_data)) {
		dev_err(&pdev->dev, "Failed to alloc backlight data.\n");
		return -ENOMEM;
	}
	bl_data->pdev = pdev;

	ret = npsc01bl_parse_display_interface(pdev->dev.of_node);
	if(ret) {
		dev_err(&pdev->dev, "Backlight driver is not loaded.\n");
		goto release_bl;
	}

	ret = npsc01bl_parse_backlight_power_func(pdev);
	if(ret) {
		dev_err(&pdev->dev, "Failed to parse bl power func.\n");
		goto release_bl;
	}

	ret = npsc01bl_notifier_register();
	if(ret) {
		dev_err(&pdev->dev, "Failed to register bl notifier.\n");
		goto release_bl;
	}

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		dev_err(&pdev->dev, "Failed to get memory resource.\n");
		return -ENXIO;
	}

	bl_data->pwm_base = ioremap(res_mem->start, resource_size(res_mem));
	if (!bl_data->pwm_base) {
		dev_err(&pdev->dev, "Failed to remap memory resource.\n");
		return -ENOMEM;
	}

	bl_data->pwm_clk = devm_clk_get(&pdev->dev, bl_data->pwm_clk_name);
//	bl_data->pwm_pclk = devm_clk_get(&pdev->dev, bl_data->pwm_pclk_name);
	if (IS_ERR(bl_data->pwm_clk)) {// || IS_ERR(bl_data->pwm_pclk)) {
		dev_err(&pdev->dev, "Failed to get pwm clock.\n");
		return -EINVAL;
	}

#if 1 //DISPLAY_DRIVER_INIT_BOOT_ON
	ret = panel_backlight_on();
	if(ret)
		dev_err(&pdev->dev, "Failed to call panel_backlight_on().\n");
	npsc01bl_set_brightness(10);
#endif

	npsc01bl_pwm_clk_io_enable();
	bl_data->bl_enabled = 1;

	memset(&bl_props, 0, sizeof(bl_props));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.max_brightness = 10;
	bl_props.power = 0;
	bl_props.brightness = 7;
	bl_props.state = FB_BLANK_UNBLANK;

	bl_dev = backlight_device_register(DRIVER_NAME, &pdev->dev, bl_data, &bl_ops, &bl_props);
	if (IS_ERR(bl_dev))
		return PTR_ERR(bl_dev);

	platform_set_drvdata(pdev, bl_dev);

	return 0;

release_bl:
#ifdef CONFIG_PINCTRL
	if(pdev->dev.pins->p) {
		pinctrl_put(pdev->dev.pins->p);
		pr_warn("Release backlight pinctrl\n");
	}
#endif

	return 0;
}

static int npsc01bl_remove(struct platform_device *pdev)
{
	struct backlight_data *bl_data;
	struct backlight_device *bl_dev;

	bl_dev = platform_get_drvdata(pdev);
	bl_data = bl_get_data(bl_dev);

	kfree(bl_data);
	backlight_device_unregister(bl_dev);

	return 0;
}

static const struct of_device_id backlight_of_match[] = {
	{ .compatible = "nufront,backlight", },
	{},
};
MODULE_DEVICE_TABLE(of, backlight_of_match);

static struct platform_driver npsc01bl_driver = {
	.probe		= npsc01bl_probe,
	.remove		= npsc01bl_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = of_match_ptr(backlight_of_match),
	},
};

static int __init npsc01bl_init(void)
{
	return platform_driver_register(&npsc01bl_driver);
}

static void __exit npsc01bl_exit(void)
{
	platform_driver_unregister(&npsc01bl_driver);
}

late_initcall(npsc01bl_init);
module_exit(npsc01bl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NPSC01 Backlight Driver.");
