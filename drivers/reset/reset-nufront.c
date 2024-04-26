#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define RESET_TRIGGER_LOW		0
#define RESET_TRIGGER_HIGH	1

union nufront_rst_key {
	struct reg_info {
		u32 reg:16;		/* reg offset: bits 0-15 */
		u32 bit:14;		/* bit index: bits 16-29 */
		u32 polarity:1; /* active polarity: bit 30 */
		u32 :1;		/* bit 31 allways 0 */
	}__packed info;
	int id;
};

struct nufront_reset_data {
	struct reset_controller_dev rcdev;
	struct regmap *regmap;
};

#define to_nufront_reset_data(p)		\
	container_of((p), struct nufront_reset_data, rcdev)

static int nufront_reset_common(struct nufront_reset_data *priv,
		u32 reg, u32 bit, u32 polarity)
{
	return regmap_update_bits(priv->regmap,
		reg, BIT_MASK(bit), polarity?BIT(bit):0);
}

static int nufront_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct nufront_reset_data *priv = to_nufront_reset_data(rcdev);
	union nufront_rst_key key;

	key.id = id;
	barrier();
	return nufront_reset_common(priv,
		key.info.reg,
		key.info.bit,
		key.info.polarity);
}

static int nufront_reset_deassert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct nufront_reset_data *priv = to_nufront_reset_data(rcdev);
	union nufront_rst_key key;

	key.id = id;
	barrier();
	return nufront_reset_common(priv,
		key.info.reg,
		key.info.bit,
		!key.info.polarity);
}


static int nufront_reset_reset(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	nufront_reset_assert(rcdev, id);
	return nufront_reset_deassert(rcdev, id);
}

static struct reset_control_ops nufront_reset_ops = {
	.reset 		= nufront_reset_reset,
	.assert		= nufront_reset_assert,
	.deassert	= nufront_reset_deassert,
};

/**
 * nufront_reset_xlate() - translation arguments of resets
 *
 * Example:
 *
 * reset_phandle1: node1 {
 * };
 *
 * device_node {
 * 	resets = <&reset_phandle1 reg bit polarity>;
 * };
 *
 * reg: the offset of register from prcm
 * bit: which bit in the register
 * polarity: polarity to active reseting, low:0 high:1
 *
*/
static int nufront_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	union nufront_rst_key key;

	/* address over follow. */
	if(reset_spec->args[1] > 0xffff)
		return -EINVAL;

	/* bit index shoud be on register range. */
	if(reset_spec->args[1] < 0 &&
		reset_spec->args[1] > BITS_PER_LONG)
		return -EINVAL;

	/* polarity */
	if(reset_spec->args[2] != RESET_TRIGGER_LOW &&
		reset_spec->args[2] != RESET_TRIGGER_HIGH)
		return -EINVAL;

	key.info.reg = reset_spec->args[0];
	key.info.bit = reset_spec->args[1];
	key.info.polarity = reset_spec->args[2];
	barrier();
	return key.id;
}

static int nufront_reset_probe(struct platform_device *pdev)
{
	struct nufront_reset_data *priv;
	struct regmap *regmap;

	regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "syscon");
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "unable to get regmap");
		return PTR_ERR(regmap);
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->regmap = regmap;
	priv->rcdev.ops = &nufront_reset_ops;
	priv->rcdev.of_node = pdev->dev.of_node;
	priv->rcdev.of_reset_n_cells = 3;
	priv->rcdev.of_xlate = nufront_reset_xlate;

	return reset_controller_register(&priv->rcdev);
}

static const struct of_device_id nufront_reset_dt_ids[] = {
	{ .compatible = "nufront,nufront-reset", },
	{ },
};
MODULE_DEVICE_TABLE(of, nufront_reset_dt_ids);

static struct platform_driver nufront_reset_driver = {
	.probe	= nufront_reset_probe,
	.driver = {
		.name		= "nufront-reset",
		.of_match_table	= nufront_reset_dt_ids,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(nufront_reset_driver);

MODULE_DESCRIPTION("Nufront Reset Controller Driver");
MODULE_AUTHOR("wangjingyang <jingyang.wang@nufront.com>");
MODULE_LICENSE("GPL");
