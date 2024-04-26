/*
 * Copyright 2017 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

//#define DEBUG

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <mach/pinctrl-nufront.h>

#include "core.h"

#include <mach/pinctrl-npsc01.h>

#define DRIVER_NAME "pinctrl-nufront"


/* scm pin config type, this enum valued by respective start-address. */
enum SCM_SETUP_TYPE {
	SCM_FUNC_MODE = 0,
	SCM_REN_CTRL = 0x30,
	SCM_PULL_CTRL = 0x48,
	SCM_DEV_STR = 0x78,
	SCM_SMT_CTRL = 0xc0,
};

struct nufront_pinctrl_private {
	struct pinctrl_dev *pctl;
	struct regmap *regs;
	struct nufront_pin_ctrl *data;
};

#define BIT_TO_REG(bit) (((bit)/32) * 4)

#ifdef DEBUG
static void nufront_debug_dump_configs(int *configs, int num_configs)
{
	int i = 0;
	enum pin_config_param cfg_type;
	u16 cfg_value;

	for(i=0; i<num_configs; i++) {
		cfg_type = pinconf_to_config_param(configs[i]);
		cfg_value = pinconf_to_config_argument(configs[i]);
		pr_debug("config[%d] type=%d value=%d\n", i, cfg_type, cfg_value);
	}
}
#endif

/*
	only for platform npsc01, the register of strenght was ranked by pad_p[3*num+2:3*num]
*/
static int nufront_pin_set_strength_only_npsc01(struct regmap *map,
		unsigned int pin, unsigned int strenght)
{
	int bit_off;
	int reg_off_low, reg_off_high;
	int shift;
	u32 data;
	u32 mask = 0x7;

	bit_off = pin * 3;
	shift = bit_off % 32;
	reg_off_low = SCM_DEV_STR + BIT_TO_REG(bit_off);
	reg_off_high = SCM_DEV_STR + BIT_TO_REG(bit_off + 2);

	regmap_read(map, reg_off_low, &data);
	data &= ~(mask << shift);
	data |= strenght << shift;
	regmap_write(map, reg_off_low, data);

	if(reg_off_low != reg_off_high) {
		shift = 32 - shift;
		regmap_read(map, reg_off_high, &data);
		data &= ~(mask >> shift);
		data |= strenght >> shift;
		regmap_write(map, reg_off_high, data);
	}

	return 0;
}

static u32 nufront_pin_get_strength_only_npsc01(struct regmap *map,
		unsigned pin)
{
	int bit_off;
	int reg_off_low, reg_off_high;
	int shift;
	u32 data;
	u32 mask = 0x7;
	u32 ret = 0;

	bit_off = pin * 3;
	shift = bit_off % 32;
	reg_off_low = SCM_DEV_STR + BIT_TO_REG(bit_off);
	reg_off_high = SCM_DEV_STR + BIT_TO_REG(bit_off + 2);

	regmap_read(map, reg_off_low, &data);
	ret |= (data>>shift) & mask;

	if(reg_off_low != reg_off_high) {
		shift = 32 - shift;
		regmap_read(map, reg_off_high, &data);
		ret |= (data & (mask>>shift))<<shift;
	}

	return ret;
}

static void nufront_get_pin_position(unsigned int pin, enum SCM_SETUP_TYPE setup,
	int *reg, int *shift, u32 *mask)
{
	u32 bundle; /* number of pins pre each register */
	u32 width;	/* number of bits pre each pin */
	u32 remaind;

	switch (setup)
	{
		case SCM_SMT_CTRL:
		case SCM_REN_CTRL:
			bundle = 32;
			width = 1;
			break;

		case SCM_FUNC_MODE:
		case SCM_PULL_CTRL:
			bundle = 16;
			width = 2;
			break;

		case SCM_DEV_STR:
			bundle = 10;
			width = 2;
			break;

		default:
			panic("Unknow setup type!\n");
			break;
	}

	*reg = div_u64_rem((u64)pin, bundle, &remaind) * 4 + setup;
	*shift = remaind * width;
	*mask = GENMASK_ULL(*shift + width - 1, *shift);
	return;
}

static int nufront_pin_set(struct pinctrl_dev *pctldev,
		unsigned pin, u32 cfg, enum SCM_SETUP_TYPE setup)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	struct regmap *map = pdata->regs;
	int mask, shift, reg;

	pr_debug("%s: pin=%d cfg=%d setup=%d\n", __func__, pin, cfg, setup);

#if CONFIG_ARCH_NPSC01
	if(setup == SCM_DEV_STR)
		return nufront_pin_set_strength_only_npsc01(map, pin, cfg);
#endif

	nufront_get_pin_position(pin, setup, &reg, &shift, &mask);
	regmap_update_bits(map, reg, mask, cfg<<shift);
	return 0;
}

static int nufront_pin_get(struct pinctrl_dev *pctldev,
		unsigned pin, enum SCM_SETUP_TYPE setup)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	struct regmap *map = pdata->regs;
	int mask, shift, reg;
	u32 data;

#if CONFIG_ARCH_NPSC01
	if(setup == SCM_DEV_STR)
		return nufront_pin_get_strength_only_npsc01(map, pin);
#endif

	nufront_get_pin_position(pin, setup, &reg, &shift, &mask);
	regmap_read(map, reg, &data);

	return (data & mask) >> shift;
}

static int nufront_bundle_set(struct pinctrl_dev *pctldev,
		const unsigned pins[], const unsigned cfg[], int nr_pins,
		enum SCM_SETUP_TYPE setup)
{
	int i = 0;

	for (i = 0; i < nr_pins ; i++ ) {
		nufront_pin_set(pctldev, pins[i], cfg[i], setup);
	}

	return 0;
}

static int __maybe_unused nufront_bundle_get(struct pinctrl_dev *pctldev,
		const unsigned pins[], unsigned cfg[], int nr_pins,
		enum SCM_SETUP_TYPE setup)
{
	int i = 0;

	for (i = 0; i < nr_pins ; i++ ) {
		cfg[i] = nufront_pin_get(pctldev, pins[i], setup);
	}

	return 0;
}

static const struct nufront_pin_group *nufront_match_group(struct pinctrl_dev *pctldev, const char *group)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pin_group *array = pdata->data->groups;
	int i;

	for (i = 0; i<pdata->data->nr_groups; i++) {
		if(!strcmp(array[i].name, group)) {
			return array + i;
		}
	}

	return NULL;
}

static const struct nufront_pmx_func *nufront_match_function(struct pinctrl_dev *pctldev, const char *function)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pmx_func *array = pdata->data->functions;
	int i;

	for (i = 0; i<pdata->data->nr_functions; i++) {
		if(!strcmp(array[i].name, function)) {
			return array + i;
		}
	}

	return NULL;
}

static int nufront_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	return pdata->data->nr_groups;
}

static const char *nufront_get_group_name(struct pinctrl_dev *pctldev,
		unsigned selector)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	if (selector >= pdata->data->nr_groups)
		return NULL;

	return pdata->data->groups[selector].name;
}

static int nufront_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned selector,
		const unsigned **pins,
		unsigned *num_pins)
{
	struct nufront_pinctrl_private *pdata =
			(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	if (selector >= pdata->data->nr_groups)
		return -EINVAL;

	*pins = pdata->data->groups[selector].pins;
	*num_pins = pdata->data->groups[selector].nr_pins;
	return 0;
}

int nufront_pinconf_subnode_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np, struct pinctrl_map **map,
		unsigned *reserved_maps, unsigned *num_maps)
{
	const char *function;
	struct device *dev = pctldev->dev;
	unsigned long *configs = NULL;
	unsigned num_configs = 0;
	unsigned reserve, strings_count;
	struct property *prop;
	const char *target;
	const char *subnode_target_type;
	enum pinctrl_map_type type = PIN_MAP_TYPE_INVALID;
	int ret;

	if ((ret = of_property_count_strings(np, "pins")) > 0) {
		type = PIN_MAP_TYPE_CONFIGS_PIN;
		subnode_target_type = "pins";
	}
	else if ((ret = of_property_count_strings(np, "groups")) > 0) {
		type = PIN_MAP_TYPE_CONFIGS_GROUP;
		subnode_target_type = "groups";
	}
	else {
		return -EINVAL;
	}

	strings_count = ret;

	ret = of_property_read_string(np, "function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev, "%s: could not parse property function\n",
				of_node_full_name(np));

		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs,
					      &num_configs);
	if (ret < 0) {
		dev_err(dev, "%s: could not parse node property\n",
			of_node_full_name(np));
		return ret;
	}

	pr_debug("parsed config number: %d\n", num_configs);

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;

	reserve *= strings_count;

	*map = krealloc(*map,
		(*num_maps+reserve) * sizeof(struct pinctrl_map), GFP_KERNEL);
	if (map == NULL) {
		dev_err(dev, "cannot allocate pinctrl_map memory for %s\n", np->name);
		goto exit;
	}

	of_property_for_each_string(np, subnode_target_type, prop, target) {

		/* declear function and group */
		if(function) {
			(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[*num_maps].data.mux.group = target;
			(*map)[*num_maps].data.mux.function = function;
			pr_debug("node_to_map: add function %s for group %s\n",
					function, target);
			(*num_maps)++;
		}

		/* config group or pin */
		if (num_configs) {
			(*map)[*num_maps].type = type;
			(*map)[*num_maps].data.configs.group_or_pin = target;
			(*map)[*num_maps].data.configs.configs =
				kmemdup(configs, num_configs*sizeof(*configs), GFP_KERNEL);
			(*map)[*num_maps].data.configs.num_configs = num_configs;
			pr_debug("node_to_map: add %d configs for group_or_pin %s\n",
					num_configs, target);
#ifdef DEBUG
			nufront_debug_dump_configs(configs, num_configs);
#endif
			(*num_maps)++;
		}
	}

	ret = 0;
exit:
	kfree(configs);
	return ret;
}

static void nufront_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
				 struct pinctrl_map *map,
				 unsigned num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++) {
		switch (map[i].type) {
		case PIN_MAP_TYPE_CONFIGS_GROUP:
		case PIN_MAP_TYPE_CONFIGS_PIN:
			kfree(map[i].data.configs.configs);
			break;
		default:
			break;
		}
	}

	kfree(map);
	return;
}

static int nufront_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
		struct device_node *np_config,
		struct pinctrl_map **map,
		unsigned *num_maps)
{
	unsigned reserved_maps;
	struct device_node *np;
	int ret;

	pr_debug("%s: start parse node %s\n", __func__, np_config->name);

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	ret = nufront_pinconf_subnode_to_map(pctldev, np_config, map,
				&reserved_maps, num_maps);
	if (ret < 0)
		goto exit;

	for_each_available_child_of_node(np_config, np) {
		ret = nufront_pinconf_subnode_to_map(pctldev, np, map,
					&reserved_maps, num_maps);
		if (ret < 0)
			goto exit;
	}

	return 0;

exit:
	dev_err(pctldev->dev, "Unrecognized node: %s.\n", np_config->name);
	nufront_pinctrl_dt_free_map(pctldev, *map, *num_maps);
	return ret;
}

static struct pinctrl_ops nufront_pctl_ops = {
	.get_groups_count = nufront_get_groups_count,
	.get_group_name = nufront_get_group_name,
	.get_group_pins = nufront_get_group_pins,
	.dt_node_to_map = nufront_pinctrl_dt_node_to_map,
	.dt_free_map = nufront_pinctrl_dt_free_map,
};

static int nufront_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	return pdata->data->nr_functions;
}

const char *nufront_pmx_get_fname(struct pinctrl_dev *pctldev, unsigned selector)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	return pdata->data->functions[selector].name;
}

static int nufront_pmx_get_groups(struct pinctrl_dev *pctldev,
		unsigned selector,
		const char * const **groups,
		unsigned * const num_groups)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);

	*groups = pdata->data->functions[selector].groups;
	*num_groups = pdata->data->functions[selector].nr_groups;
	return 0;
}

int nufront_pmx_enable(struct pinctrl_dev *pctldev,
	unsigned func_selector,
	unsigned group_selector)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pin_group *grp =
		pdata->data->groups + group_selector;

	nufront_bundle_set(pctldev, grp->pins, grp->func, grp->nr_pins, SCM_FUNC_MODE);

	if(grp->pull) {
		nufront_bundle_set(pctldev, grp->pins, grp->pull, grp->nr_pins, SCM_PULL_CTRL);
	}

	if(grp->strength) {
		nufront_bundle_set(pctldev, grp->pins, grp->strength, grp->nr_pins, SCM_DEV_STR);
	}

	return 0;
}

void nufront_pmx_disable(struct pinctrl_dev *pctldev,
	unsigned func_selector,
	unsigned group_selector)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pin_group *grp =
		pdata->data->groups + group_selector;
	u32 *cfg_set;

	cfg_set = (u32*)kzalloc(grp->nr_pins * sizeof(u32), GFP_KERNEL);

	nufront_bundle_set(pctldev, grp->pins, cfg_set, grp->nr_pins, SCM_FUNC_MODE);
	nufront_bundle_set(pctldev, grp->pins, cfg_set, grp->nr_pins, SCM_PULL_CTRL);
	nufront_bundle_set(pctldev, grp->pins, cfg_set, grp->nr_pins, SCM_DEV_STR);

	kfree(cfg_set);
	return;
}


static int nufront_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned pin)
{
	const struct nufront_pin_group *grp;
	int offset;

	grp = nufront_match_group(pctldev, range->name);
	if (!grp)
		return -1;

	for (offset = 0; offset < grp->nr_pins; offset++) {
		if (grp->pins[offset] == pin) {
			return nufront_pin_set(pctldev, pin,
					grp->func[offset], SCM_FUNC_MODE);
		}
	}

	return -2;
}

static void nufront_pmx_gpio_request_free(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned pin)
{
	nufront_pin_set(pctldev, pin, 0, SCM_FUNC_MODE);
	return;
}

static int nufront_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned pin,
				    bool input)
{
	return 0;
}

static int gpiochip_match_group(struct gpio_chip *chip, void *data)
{
	const char *name = (char*)data;
	return !strcmp(chip->label, name);
}


static int nufront_pmx_gpio_range_init(struct platform_device *pdev,
	struct pinctrl_dev *pctldev)
{
	const struct nufront_pin_group *grp;
	const struct nufront_pmx_func *func;
	struct pinctrl_gpio_range *range;
	struct gpio_chip *chip;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *gpio_np;
	int i = 0;

	func = nufront_match_function(pctldev, "gpio");

	for(i=0; i<func->nr_groups; ++i) {

		gpio_np = of_parse_phandle(np, "gpio-chips", i);

		chip = gpiochip_find((void*)of_node_full_name(gpio_np), gpiochip_match_group);
		if(!chip) {
			dev_err(&pdev->dev, "chip %s not found!\n", gpio_np->name);
			return -EINVAL;
		}

		grp = nufront_match_group(pctldev, func->groups[i]);
		if(!grp) {
			dev_err(&pdev->dev, "group %s not found!\n", func->groups[i]);
			return -EINVAL;
		}

		range = devm_kzalloc(pctldev->dev,
				sizeof(struct pinctrl_gpio_range), GFP_KERNEL);
		if(!range)
			return -ENOMEM;

		range->name = grp->name;
		range->id = i;
		range->base = chip->base;
		range->pin_base = 0;
		range->pins = grp->pins;
		range->npins = grp->nr_pins;
		range->gc = chip;
		pinctrl_add_gpio_range(pctldev, range);
		pr_debug("add gpio %s to pinctrl range, it has %d pins base %d.\n",
			range->name, range->npins, range->base);
	}

	return 0;
}

static struct pinmux_ops nufront_pmxops = {
	.get_functions_count = nufront_get_functions_count,
	.get_function_name = nufront_pmx_get_fname,
	.get_function_groups = nufront_pmx_get_groups,
	.enable = nufront_pmx_enable,
	.disable = nufront_pmx_disable,
	.gpio_request_enable = nufront_pmx_gpio_request_enable,
	.gpio_disable_free = nufront_pmx_gpio_request_free,
	.gpio_set_direction = nufront_pmx_gpio_set_direction,
};

static int nufront_pin_config_get(struct pinctrl_dev *pctldev,
	unsigned pin,
	unsigned long *config)
{
	enum pin_config_param cfg_type = pinconf_to_config_param(*config);
	u32 cfg_value;
	u32 data;

	pr_debug("%s: pin=%d type=%d\n",
		__func__, pin, cfg_type);

	switch (cfg_type)
	{
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			data = nufront_pin_get(pctldev, pin, SCM_PULL_CTRL);

			if(cfg_type == PIN_CONFIG_BIAS_HIGH_IMPEDANCE)
				cfg_value = data==0?1:0;
			else if(cfg_type == PIN_CONFIG_BIAS_PULL_UP)
				cfg_value = data==1?1:0;
			else if(cfg_type == PIN_CONFIG_BIAS_PULL_DOWN)
				cfg_value = data==2?1:0;

			*config = pinconf_to_config_packed(cfg_type, cfg_value);
			break;

		case PIN_CONFIG_INPUT_SCHMITT:
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			cfg_value = nufront_pin_get(pctldev, pin, SCM_SMT_CTRL);
			*config = pinconf_to_config_packed(cfg_type, cfg_value);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			cfg_value = nufront_pin_get(pctldev, pin, SCM_DEV_STR);
			*config = pinconf_to_config_packed(cfg_type, cfg_value);
			break;

		default:
			break;
	}

	return 0;
}

static int nufront_pin_config_set(struct pinctrl_dev *pctldev,
	unsigned pin,
	unsigned long config)
{
	enum pin_config_param cfg_type = pinconf_to_config_param(config);
	u16 cfg_value = pinconf_to_config_argument(config);
	u32 data;
	int ret = 0;

	pr_debug("%s: pin=%d type=%d cfg=%d\n",
		__func__, pin, cfg_type, cfg_value);

	switch (cfg_type)
	{
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if(cfg_type == PIN_CONFIG_BIAS_HIGH_IMPEDANCE)
				data = 0;
			else if(cfg_type == PIN_CONFIG_BIAS_PULL_UP)
				data = cfg_value==1?1:0;
			else if(cfg_type == PIN_CONFIG_BIAS_PULL_DOWN)
				data = cfg_value==1?2:0;
			ret = nufront_pin_set(pctldev, pin, data, SCM_PULL_CTRL);
			break;

		case PIN_CONFIG_INPUT_SCHMITT:
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			data = cfg_value?1:0;
			ret = nufront_pin_set(pctldev, pin, data, SCM_SMT_CTRL);
			break;

		case PIN_CONFIG_DRIVE_STRENGTH:
			data = cfg_value>7?7:cfg_value;
			ret = nufront_pin_set(pctldev, pin, data, SCM_DEV_STR);
			break;

		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static int nufront_group_config_get(struct pinctrl_dev *pctldev,
				 unsigned selector,
				 unsigned long *config)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pin_group *grp =
		pdata->data->groups + selector;

	return nufront_pin_config_get(pctldev, grp->pins[0], config);
}

static int nufront_group_config_set(struct pinctrl_dev *pctldev,
				 unsigned selector,
				 unsigned long config)
{
	struct nufront_pinctrl_private *pdata =
		(struct nufront_pinctrl_private*)pinctrl_dev_get_drvdata(pctldev);
	const struct nufront_pin_group *grp =
		pdata->data->groups + selector;
	int i = 0;
	int ret = 0;

	for(i=0; i < grp->nr_pins; i++) {
		ret = nufront_pin_config_set(pctldev, grp->pins[i], config);
		if(ret)
			break;
	}

	return ret;
}

static struct pinconf_ops nufront_confops = {
	.pin_config_get = nufront_pin_config_get,
	.pin_config_set = nufront_pin_config_set,
	.pin_config_group_get = nufront_group_config_get,
	.pin_config_group_set = nufront_group_config_set,
};


static struct pinctrl_desc nufront_pmx_desc = {
	.name = DRIVER_NAME,
	.owner = THIS_MODULE,
	.pctlops = &nufront_pctl_ops,
	.pmxops = &nufront_pmxops,
	.confops = &nufront_confops,
};

static const struct of_device_id nufront_pinctrl_of_match[] = {
	{ .compatible = "nufront,npsc01-pinctrl" , .data = (void *)&npsc01_pin_ctrl},
	{ }
};

static int nufront_pmx_probe(struct platform_device *pdev)
{
	int ret;
	struct nufront_pinctrl_private *nupctl;
	const struct of_device_id *of_id =
			of_match_device(nufront_pinctrl_of_match, &pdev->dev);

	if (!of_id || !of_id->data) {
		dev_err(&pdev->dev, "could not match any nufront's pinmux driver!\n");
		return -ENOMEM;
	}

	nupctl = devm_kzalloc(&pdev->dev, sizeof(*nupctl), GFP_KERNEL);
	if (!nupctl)
		return -ENOMEM;

	nupctl->regs = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "syscon");
	if (!nupctl->regs)
		return -ENOENT;

	nupctl->data = (struct nufront_pin_ctrl*) of_id->data;
	nufront_pmx_desc.pins = nupctl->data->pins;
	nufront_pmx_desc.npins = nupctl->data->nr_pins;
	nupctl->pctl = pinctrl_register(&nufront_pmx_desc, &pdev->dev, nupctl);
	if (!nupctl->pctl) {
		dev_err(&pdev->dev, "could not register nufront pinmux driver\n");
		ret = -EINVAL;
		goto error_no_pmx;
	}

	nufront_pmx_gpio_range_init(pdev, nupctl->pctl);

	platform_set_drvdata(pdev, nupctl);
	pr_info("Registered pinmux driver %s succesed.\n", pdev->name);
	return 0;

error_no_pmx:
	return ret;
}

static int nufront_pmx_remove(struct platform_device *pdev)
{
	struct nufront_pinctrl_private *nupctl = platform_get_drvdata(pdev);

	pinctrl_unregister(nupctl->pctl);
	kfree(nupctl);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver nufront_pinctrl_driver = {
	.probe = nufront_pmx_probe,
	.remove = nufront_pmx_remove,
	.driver = {
		.name = "nufront-pinctrl",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nufront_pinctrl_of_match),
	},
};

static int __init nufront_pinctrl_init(void)
{
	return platform_driver_register(&nufront_pinctrl_driver);
}

arch_initcall_sync(nufront_pinctrl_init);

MODULE_AUTHOR("wang jingyang <jingyang.wang@nufront.com>");
MODULE_DESCRIPTION("Nufront pin control driver v2");
MODULE_LICENSE("GPL v2");
