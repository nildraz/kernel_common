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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/gpio.h>

/*
	For receive dual edge interrupt in same time, two of pins are wrapped
	together, each wrapper feed to bit[n] and bit[n+16].
*/
#define DUAL_EDGE_SUPPORT 1

#if DUAL_EDGE_SUPPORT
#define BANK_OFFSET 16
#define BANK_MASK 0xffff
#endif

#define GPIO_PORT_OFFSET     0xc

#define GPIO_PORT_DR         0x0
#define GPIO_PORT_DDR        0x4
#define GPIO_PORT_CTRL       0x8
#define GPIO_PORT_EXT_OFFSET 0x4
#define GPIO_PORT_EXT        0x50
#define GPIO_INTEN           0x30
#define GPIO_INTMASK         0x34
#define GPIO_INTTYPE_LEVEL   0x38
#define GPIO_INT_POLARITY    0x3c
#define GPIO_INTSTATUS       0x40
#define GPIO_RAW_INTSTATUS   0x44
#define GPIO_DEBOUNCE        0x48
#define GPIO_PORTA_EOI       0x4c
#define GPIO_ID_CODE         0x64
#define GPIO_VER_ID_CODE     0x6C
#define GPIO_CONFIG_REG1     0x6C
#define GPIO_CONFIG_REG2     0x6C

struct nufront_gpio_context {
	u32 data;
	u32 dir;
	u32 int_en;
	u32 int_mask;
	u32 int_type;
	u32 int_pol;
	u32 int_deb;
};

#define npsc_gpio_read(base, group, reg) readl(base+(group*GPIO_PORT_OFFSET)+reg)
#define npsc_gpio_write(val, base, group, reg) writel(val, base+(group*GPIO_PORT_OFFSET)+reg)
#define npsc_gpio_ext_read(base, group) readl(base+(group*GPIO_PORT_EXT_OFFSET)+GPIO_PORT_EXT)

struct nufront_gpio_group {
	unsigned int npins;
	u32 valid_mask;
};

struct nufront_gpio_priv {
	struct gpio_chip chip;
	struct irq_chip irq_chip;
	void __iomem *regbase;
	int irq;
	struct irq_domain *domain;
	unsigned int irq_base;
	unsigned int base;
	unsigned int ngroups;
	unsigned int npins;
	struct nufront_gpio_group groups[4];
	bool wk_source;
#ifdef CONFIG_PM_SLEEP
	struct nufront_gpio_context ctx;
#endif
};

static int npsc_offset_to_location(
	struct nufront_gpio_priv *priv,
	unsigned int offset,
	unsigned int *group,unsigned int *pin)
{
	unsigned int pn = 0, gp = 0;
	unsigned int pass = 0;

	if (offset > priv->npins)
		return -EINVAL;

	for(gp=0; gp < priv->ngroups; gp++) {

		if(offset < priv->groups[gp].npins+pass) {
			pn = offset-pass;
			break;
		}
		pass += priv->groups[gp].npins;
	}
	*group = gp;
	*pin = pn;
	return 0;
}

static int nufront_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;
	return pinctrl_request_gpio(gpio);
}

void nufront_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int gpio = chip->base + offset;
	pinctrl_free_gpio(gpio);
	return;
}


static void nufront_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->regbase;
	unsigned long data;
	unsigned int pin, group;

	if(npsc_offset_to_location(priv, offset, &group, &pin)) {
		return;
	}

	data = npsc_gpio_read(base, group, GPIO_PORT_DR);
	data &= ~(BIT(pin));
	if (value)
		data |= BIT(pin);
	npsc_gpio_write(data, base, group, GPIO_PORT_DR);
}

static int nufront_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->regbase;
	unsigned long data;
	unsigned int pin, group;

	if(npsc_offset_to_location(priv, offset, &group, &pin)) {
		return -EINVAL;
	}
	data = npsc_gpio_ext_read(base, group);
	return test_bit(pin, &data);
}

static int nufront_gpio_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->regbase;
	unsigned long data;
	unsigned int pin, group;

	if(npsc_offset_to_location(priv, offset, &group, &pin)) {
		return -EINVAL;
	}
	data = npsc_gpio_read(base, group, GPIO_PORT_DDR);
	return !test_bit(pin, &data);
}

static int nufront_gpio_set_direction(struct gpio_chip *gc, unsigned offset, int dir)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->regbase;
	unsigned long data;
	unsigned int pin, group;

	if(npsc_offset_to_location(priv, offset, &group, &pin)) {
		return -EINVAL;
	}
	data = npsc_gpio_read(base, group, GPIO_PORT_DDR);
	if(dir) //input
		clear_bit(pin, &data);
	else  //output
		set_bit(pin, &data);
	npsc_gpio_write(data, base, group, GPIO_PORT_DDR);
	return 0;
}

static int nufront_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return nufront_gpio_set_direction(gc, offset, 1);
}

static int nufront_gpio_direction_output(struct gpio_chip *gc, unsigned offset, int value)
{
	int ret = 0;

	ret = nufront_gpio_set_direction(gc, offset, 0);
	if(ret) {
		return ret;
	}
	nufront_gpio_set(gc, offset, value);

	return 0;
}

static int nufront_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	return irq_find_mapping(priv->domain, offset);
}

static int nufront_gpio_set_debounce(struct gpio_chip *gc, unsigned offset, unsigned debounce)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->regbase;
	unsigned long data;
	unsigned int pin, group;

	if(npsc_offset_to_location(priv, offset, &group, &pin))
		return -EINVAL;

	if(group != 0)
		return -EINVAL;

	data = readl(base + GPIO_DEBOUNCE);
	data &= ~(BIT(pin));
	if (debounce)
		data |= BIT(pin);

#ifdef DUAL_EDGE_SUPPORT
	data &= ~(BIT(pin+BANK_OFFSET));
	if (debounce)
		data |= BIT(pin+BANK_OFFSET);
#endif
	writel(data, base + GPIO_DEBOUNCE);
	return 0;
}

static struct gpio_chip npsc_gpio_chip = {
	.request = nufront_gpio_request,
	.free = nufront_gpio_free,
	.set = nufront_gpio_set,
	.get = nufront_gpio_get,
	.get_direction = nufront_gpio_get_direction,
	.direction_input = nufront_gpio_direction_input,
	.direction_output = nufront_gpio_direction_output,
	.to_irq = nufront_gpio_to_irq,
	.set_debounce = nufront_gpio_set_debounce,
};

static void nufront_gpio_register_chip(const char *name, struct platform_device	*pdev, struct nufront_gpio_priv *priv)
{
	struct gpio_chip *gc = &priv->chip;
	int ret = 0;

	*gc = npsc_gpio_chip;
	gc->base = priv->base;
	gc->ngpio = priv->npins;
	gc->dev = &pdev->dev;
	gc->label = name;
	gc->owner = THIS_MODULE;
	ret = gpiochip_add(gc);
}

static void nufront_gpio_init_controller(struct platform_device *pdev, struct nufront_gpio_priv *priv)
{
	void __iomem *base = priv->regbase;
	int group;

	for(group=0; group<priv->ngroups; group++) {
		//clear pins
		npsc_gpio_write(0, base, group, GPIO_PORT_DR);
		npsc_gpio_write(0, base, group, GPIO_PORT_DDR);
	}

	writel(0, base + GPIO_INTEN);
	writel(0, base + GPIO_INTTYPE_LEVEL);
	writel(0, base + GPIO_INT_POLARITY);
	writel(0, base + GPIO_INTMASK);
	writel(0xFFFFFFFF, base + GPIO_PORTA_EOI);
}

static void npsc_gpio_irq_disable(struct irq_data *d)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	unsigned long data;

	data = readl(base + GPIO_INTEN);
	clear_bit(offset, &data);
#ifdef DUAL_EDGE_SUPPORT
	clear_bit(offset+BANK_OFFSET, &data);
#endif
	writel(data, base + GPIO_INTEN);
	pr_debug("%s 0x%x offset %d\n", __func__, (unsigned int)data, offset);
}

static void npsc_gpio_irq_enable(struct irq_data *d)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	unsigned long data;

	data = readl(base + GPIO_INTEN);
	set_bit(offset, &data);
#ifdef DUAL_EDGE_SUPPORT
	set_bit(offset+BANK_OFFSET, &data);
#endif
	writel(data, base + GPIO_INTEN);
	pr_debug("%s 0x%x offset %d\n", __func__, (unsigned int)data, offset);
}

static void npsc_gpio_irq_ack(struct irq_data *d)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	unsigned long data;

	set_bit(offset, &data);
#ifdef DUAL_EDGE_SUPPORT
	set_bit(offset+BANK_OFFSET, &data);
#endif
	writel(data, base + GPIO_PORTA_EOI);
	pr_debug("%s 0x%x offset %d\n", __func__, (unsigned int)data, offset);
}

static void npsc_gpio_irq_mask(struct irq_data *d)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	unsigned long data;

	data = readl(base + GPIO_INTMASK);
	set_bit(offset, &data);
#ifdef DUAL_EDGE_SUPPORT
	set_bit(offset+BANK_OFFSET, &data);
#endif
	writel(data, base + GPIO_INTMASK);
	pr_debug("%s 0x%x offset %d\n", __func__, (unsigned int)data, offset);
}

static void npsc_gpio_irq_unmask(struct irq_data *d)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	unsigned long data;

	data = readl(base + GPIO_INTMASK);
	clear_bit(offset, &data);
#ifdef DUAL_EDGE_SUPPORT
	clear_bit(offset+BANK_OFFSET, &data);
#endif
	writel(data, base + GPIO_INTMASK);
	pr_debug("%s 0x%x offset %d\n", __func__, (unsigned int)data, offset);
}

static int hw_irq_set_type(void __iomem *base, u32 offset, u32 type)
{
	unsigned long level, polarity;

	level = readl(base + GPIO_INTTYPE_LEVEL);
	polarity = readl(base + GPIO_INT_POLARITY);

	switch(type) {
	case IRQ_TYPE_EDGE_RISING:
		set_bit(offset, &level);
		set_bit(offset, &polarity);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		set_bit(offset, &level);
		clear_bit(offset, &polarity);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		clear_bit(offset, &level);
		set_bit(offset, &polarity);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		clear_bit(offset, &level);
		clear_bit(offset, &polarity);
		break;
	default:
		break;
	}

	writel(level, base + GPIO_INTTYPE_LEVEL);
	writel(polarity, base + GPIO_INT_POLARITY);
	return 0;
}

#ifdef DUAL_EDGE_SUPPORT

static int npsc_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;
	int err = 0;

	if(IRQ_TYPE_EDGE_BOTH == type) {
		err |= hw_irq_set_type(base, offset, IRQ_TYPE_EDGE_RISING);
		err |= hw_irq_set_type(base, offset+BANK_OFFSET, IRQ_TYPE_EDGE_FALLING);
	}
	else {
		err |= hw_irq_set_type(base, offset, type);
		err |= hw_irq_set_type(base, offset+BANK_OFFSET, type);
	}

	return err;
}

#else

static int npsc_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned int offset = irqd_to_hwirq(d);
	void __iomem *base = priv->regbase;

	return hw_irq_set_type(base, offset, type);
}
#endif

static int npsc_gpio_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct nufront_gpio_priv *priv = irq_data_get_irq_chip_data(d);

	return irq_set_irq_wake(priv->irq, on);
}

static struct lock_class_key npsc_gpio_lock_class;

static void nufront_gpio_irq_dispatcher(unsigned int irq, struct irq_desc *desc)
{
	struct nufront_gpio_priv *priv = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	void __iomem *base = priv->regbase;
	int offset = 0;
	unsigned long irqstat;

	chained_irq_enter(chip, desc);

	irqstat = readl(base+GPIO_INTSTATUS);
	writel(irqstat, base+GPIO_PORTA_EOI);

#ifdef DUAL_EDGE_SUPPORT
	irqstat = (irqstat>>BANK_OFFSET) | (irqstat & BANK_MASK);
#endif
	irqstat &= priv->groups[0].valid_mask;

	while(irqstat) {

		if(irqstat & 1)
			generic_handle_irq(irq_find_mapping(priv->domain, offset));

		// shift right for next irq
		irqstat = irqstat>>1;
		offset++;
	}

	chained_irq_exit(chip, desc);
}

static void nufront_gpio_init_irq(struct platform_device *pdev, struct nufront_gpio_priv *priv)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	int offset = 0;
	int irq;
	int irq_base;

	irq_base = irq_alloc_descs_from(priv->irq_base, priv->npins, 0);
	priv->domain = irq_domain_add_legacy(node, priv->npins,
							irq_base, 0,
							&irq_domain_simple_ops, NULL);

	//interrupt only support portA
	for(offset=0; offset < priv->groups[0].npins; offset++) {
		irq = irq_create_mapping(priv->domain, offset);
		irq_set_lockdep_class(irq, &npsc_gpio_lock_class);
		irq_set_chip_data(irq, priv);
		irq_set_chip_and_handler(irq, &priv->irq_chip, handle_simple_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	irq_set_chained_handler(priv->irq, nufront_gpio_irq_dispatcher);
	irq_set_handler_data(priv->irq, priv);
}

static void gpio_show_version(struct nufront_gpio_priv *priv)
{
	void __iomem *base = priv->regbase;

	pr_debug("gpio %x version:%x config1:%x config2:%x\n",
		readl(base+GPIO_ID_CODE),
		readl(base+GPIO_VER_ID_CODE),
		readl(base+GPIO_CONFIG_REG1),
		readl(base+GPIO_CONFIG_REG2));
}

static const struct of_device_id nufront_gpio_of_match[];
static int nufront_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	struct nufront_gpio_priv *priv = NULL;
	struct resource *res;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);

	if (of_property_read_u32(np, "num_groups", &val)) {
		dev_err(&pdev->dev, "Invalid number of groups.\n");
		return -ENODEV;
	}
	priv->ngroups = val;

	if (of_property_read_u32(np, "base", &val)) {
		dev_err(&pdev->dev, "Invalid base number.\n");
		return -ENODEV;
	}
	priv->base = val;

	if (of_property_read_u32(np, "irq_base", &val)) {
		dev_err(&pdev->dev, "Invalid irq base number.\n");
		return -ENODEV;
	}
	priv->irq_base = val;

	if (of_property_read_bool(np, "wakeup-source")) {
		priv->wk_source = true;
	}

	for(i=0; i<priv->ngroups; i++) {
		if (of_property_read_u32_index(np, "num_pins", i, &val)) {
			dev_err(&pdev->dev, "Invalid pins of group%d.\n", i);
			return -ENODEV;
		}
		priv->groups[i].npins = val;
		priv->groups[i].valid_mask = ~((~0)<<val);
		priv->npins += val;
		pr_debug("%s group=%d num_pins=%d\n", __func__,
				i, priv->groups[i].npins);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->regbase))
		return PTR_ERR(priv->regbase);

	val = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(&pdev->dev, "Invalid IRQ resource\n");
		return -ENODEV;
	}
	priv->irq = val;

	priv->irq_chip.irq_disable = npsc_gpio_irq_disable;
	priv->irq_chip.irq_enable = npsc_gpio_irq_enable;
	priv->irq_chip.irq_ack = npsc_gpio_irq_ack;
	priv->irq_chip.irq_mask = npsc_gpio_irq_mask;
	priv->irq_chip.irq_unmask = npsc_gpio_irq_unmask;
	priv->irq_chip.irq_set_type = npsc_gpio_irq_set_type;
	if (priv->wk_source) {
		priv->irq_chip.irq_set_wake = npsc_gpio_irq_set_wake;
		priv->irq_chip.flags = 0;
	}

	gpio_show_version(priv);
	nufront_gpio_register_chip(of_node_full_name(np), pdev, priv);
	nufront_gpio_init_controller(pdev, priv);
	nufront_gpio_init_irq(pdev, priv);
	platform_set_drvdata(pdev, priv);

	pr_info("gpio %s register succesed.\n", pdev->name);
	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int nufront_gpio_suspend(struct device *dev)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(dev);
	struct nufront_gpio_context *ctx = &priv->ctx;
	void __iomem *base = priv->regbase;

	pr_debug("%s\n", __func__);
	if (priv->wk_source)
		return 0;

	ctx->data = readl(base + GPIO_PORT_DR);
	ctx->dir = readl(base + GPIO_PORT_DDR);
	ctx->int_en = readl(base + GPIO_INTEN);
	ctx->int_mask = readl(base + GPIO_INTMASK);
	ctx->int_type = readl(base + GPIO_INTTYPE_LEVEL);
	ctx->int_pol = readl(base + GPIO_INT_POLARITY);
	ctx->int_deb = readl(base + GPIO_DEBOUNCE);
	return 0;
}

static int nufront_gpio_resume(struct device *dev)
{
	struct nufront_gpio_priv *priv = dev_get_drvdata(dev);
	struct nufront_gpio_context *ctx = &priv->ctx;
	void __iomem *base = priv->regbase;

	pr_debug("%s\n", __func__);
	if (priv->wk_source)
		return 0;

	writel(ctx->data, base + GPIO_PORT_DR);
	writel(ctx->dir, base + GPIO_PORT_DDR);
	writel(ctx->int_en, base + GPIO_INTEN);
	writel(ctx->int_mask, base + GPIO_INTMASK);
	writel(ctx->int_type, base + GPIO_INTTYPE_LEVEL);
	writel(ctx->int_pol, base + GPIO_INT_POLARITY);
	writel(ctx->int_deb, base + GPIO_DEBOUNCE);
	return 0;
}
#endif

static const struct dev_pm_ops nufront_gpio_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(nufront_gpio_suspend, nufront_gpio_resume)
};

static const struct of_device_id nufront_gpio_of_match[] = {
	{ .compatible = "nufront,npsc-apb-gpio", },
	{},
};

static struct platform_driver nufront_gpio_driver = {
	.probe = nufront_gpio_probe,
	.driver	= {
		.name	= "nufront_gpio",
		.owner	= THIS_MODULE,
		.of_match_table = nufront_gpio_of_match,
		.pm	= &nufront_gpio_pm_ops,
	},
};

static int __init nufront_gpio_init(void)
{
	return platform_driver_register(&nufront_gpio_driver);
}

arch_initcall(nufront_gpio_init);

MODULE_AUTHOR("jingyang.wang@nufront.com");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Driver for Nufront Nusmart SoC GPIOs");
