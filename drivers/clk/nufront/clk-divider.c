#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/spinlock.h>

struct nufront_clk_divider {
	struct clk_divider core;
	struct clk_ops	ops;
	u8 en_bit_idx;
	spinlock_t	*lock;
};

#define to_clk_core(_hw) container_of(_hw, struct clk_divider, hw)

#define to_clk_divider(_hw) container_of(to_clk_core(_hw), struct nufront_clk_divider, core)


static int nufront_clk_divider_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate)
{
	struct nufront_clk_divider *divider = to_clk_divider(hw);
	void __iomem *addr = divider->core.reg;
	unsigned long reg_value;
	unsigned long flags = 0;
	int ret;

	if (divider->lock) {
		WARN_ON(spin_is_locked(divider->lock));
		spin_lock_irqsave(divider->lock, flags);
	}

	reg_value = readl(addr);
	clear_bit(divider->en_bit_idx, &reg_value);
	writel(reg_value, addr);

	ret = clk_divider_ops.set_rate(hw, rate, parent_rate);
	reg_value = readl(addr);

	reg_value = readl(addr);
	set_bit(divider->en_bit_idx, &reg_value);
	writel(reg_value, addr);

	if (divider->lock)
		spin_unlock_irqrestore(divider->lock, flags);

	return ret;
}

struct clk *nufront_clk_register_divider_table(
	struct device *dev, const char *name,
	const char *parent_name, unsigned long flags,
	void __iomem *reg, u8 shift, u8 width, u8 en_bit_idx,
	u8 clk_divider_flags, const struct clk_div_table *table,
	spinlock_t *lock)
{
	struct nufront_clk_divider *divider;
	struct clk_divider *core;
	struct clk *clk;
	struct clk_init_data init;
	struct clk_ops *ops;

	/* allocate the divider */
	divider = kzalloc(sizeof(struct nufront_clk_divider), GFP_KERNEL);
	if (!divider) {
		pr_err("%s: could not allocate divider clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}
	core = &divider->core;
	ops = &divider->ops;

	/* base on standard divider's ops */
	*ops = clk_divider_ops;
	ops->set_rate = nufront_clk_divider_set_rate;

	init.name = name;
	init.ops = ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_divider assignments */
	core->reg = reg;
	core->shift = shift;
	core->width = width;
	core->flags = clk_divider_flags;
	core->lock = NULL;
	core->hw.init = &init;
	core->table = table;

	/* fill local nufront_clk_divider */
	divider->en_bit_idx = en_bit_idx;
	divider->lock = lock;

	/* register the clock */
	clk = clk_register(dev, &core->hw);

	if (IS_ERR(clk))
		kfree(divider);

	return clk;
}

struct clk *nufront_clk_register_divider(
	struct device *dev, const char *name,
	const char *parent_name, unsigned long flags,
	void __iomem *reg, u8 shift, u8 width, u8 en_bit_idx,
	u8 clk_divider_flags, spinlock_t *lock)
{
	return nufront_clk_register_divider_table(dev, name,
		parent_name, flags,
		reg, shift, width, en_bit_idx,
		clk_divider_flags, NULL, lock);
}

MODULE_DESCRIPTION("Nufront Divider Clock Driver");
MODULE_AUTHOR("jingyang.wang <jingyang.wang@nufront.com>");
MODULE_LICENSE("GPL");
