#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/err.h>

static void __iomem *_sys_prcm_addr;
static struct regmap *_sys_prcm_regmap;
static struct regmap *_sys_scm_regmap;


void __iomem *get_prcm_base(void)
{
	return _sys_prcm_addr;
}
EXPORT_SYMBOL(get_prcm_base);

int nufront_map_prcm(void)
{
	struct device_node *np;

	/* prcm */
	np = of_find_compatible_node(NULL, NULL, "nufront,prcm");
	if (IS_ERR(np)) {
		pr_err("%s: failed to find prcm\n", __func__);
		return -ENODEV;
	}
	_sys_prcm_addr = of_iomap(np, 0);
	if (!_sys_prcm_addr) {
		pr_err("%s: Can't map prcm registers\n", __func__);
		return -ENODEV;
	}

	return 0;
}

int nufront_prcm_write(u32 val, u32 reg)
{
	return regmap_write(_sys_prcm_regmap, reg, val);
}
EXPORT_SYMBOL(nufront_prcm_write);

int nufront_prcm_read(u32 *val, u32 reg)
{
	return regmap_read(_sys_prcm_regmap, reg, val);
}
EXPORT_SYMBOL(nufront_prcm_read);

int nufront_scm_write(u32 val, u32 reg)
{
	return regmap_write(_sys_scm_regmap, reg, val);
}
EXPORT_SYMBOL(nufront_scm_write);

int nufront_scm_read(u32 *val, u32 reg)
{
	return regmap_read(_sys_scm_regmap, reg, val);
}
EXPORT_SYMBOL(nufront_scm_read);

int __init nufront_init_apbmisc(void)
{
	printk("%s\n", __func__);
	_sys_prcm_regmap = syscon_regmap_lookup_by_compatible("nufront,prcm");
	if (IS_ERR(_sys_prcm_regmap)) {
		pr_err("%s: regmap failed to find prcm\n", __func__);
		return -ENODEV;
	}

	/* scm */
	_sys_scm_regmap = syscon_regmap_lookup_by_compatible("nufront,scm");
	if (IS_ERR(_sys_scm_regmap)) {
		pr_err("%s: regmap failed to find scm\n", __func__);
		return -ENODEV;
	}

	return 0;
}

arch_initcall(nufront_init_apbmisc);
