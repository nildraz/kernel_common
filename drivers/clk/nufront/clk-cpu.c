#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include "clk.h"

enum {
	SRC_PLL0,
	SRC_PLL0_DIV,
	SRC_PLL4,
	SRC_PLL4_DIV,
	SRC_SYSCLK,
	SRC_MAX,
};

struct clk_cpu {
	struct clk_hw   hw;
	void __iomem *reg;
	int num_parents;
	struct clk *source[SRC_MAX];
	u8 flags;
	spinlock_t *lock;
};
#define to_clk_cpu(_hw) container_of(_hw, struct clk_cpu, hw)
/*==============================================================*/
#define CPU_MUX0_SEL_POS 0
#define CPU_MUX0_SEL_LEN 3
#define CPU_MUX0_SEL_MSK GENMASK(CPU_MUX0_SEL_POS+CPU_MUX0_SEL_LEN, CPU_MUX0_SEL_POS)

#define CPU_MUX0_EN_POS 4
#define CPU_MUX0_EN_LEN 1

#define CPU_MUX1_SEL_POS 8
#define CPU_MUX1_SEL_LEN 3
#define CPU_MUX1_SEL_MSK GENMASK(CPU_MUX1_SEL_POS+CPU_MUX1_SEL_LEN, CPU_MUX1_SEL_POS)

#define CPU_MUX1_EN_POS 12
#define CPU_MUX1_EN_LEN 1

#define CPU_MUL_SEL_POS 16
#define CPU_MUL_SEL_LEN 1

#define CPU_AUTO_SEL_POS 20
#define CPU_AUTO_SEL_LEN 1

#define CPU_AUTOMUX_SEL_POS 24
#define CPU_AUTOMUX_SEL_LEN 3

#define CPU_AUTOMUX_EN_POS 28
#define CPU_AUTOMUX_EN_LEN 1
/*==============================================================*/

/*
 *
 *
 *           mux0 \
 * source->        > mux_switch --> out
 *           mux1 /
 */
static int clk_cpu_manual_current_source(struct clk_cpu *cpuclk)
{
	unsigned long data;
	int src;

	data = readl(cpuclk->reg);

	if (!test_bit(CPU_MUL_SEL_POS, &data))
		src = (data & ~CPU_MUX0_SEL_MSK)>>CPU_MUX0_SEL_POS;
	else
		src = (data & ~CPU_MUX1_SEL_MSK)>>CPU_MUX1_SEL_POS;

	return src;
}

static void clk_cpu_manual_switch_source(struct clk_cpu *cpuclk, int src)
{
	unsigned long data = readl(cpuclk->reg);
	unsigned long flags = 0;
	spinlock_t *lock = cpuclk->lock;
	int curMux;

	if (lock) {
		WARN_ON(spin_is_locked(lock));
		spin_lock_irqsave(lock, flags);
	}

	/* disable both switch load */
	clear_bit(CPU_MUX0_EN_POS, &data);
	clear_bit(CPU_MUX1_EN_POS, &data);
	writel(data, cpuclk->reg);
	udelay(1);

	curMux = test_bit(CPU_MUL_SEL_POS, &data)?0:1;
	if (0 == curMux) {
		data &= CPU_MUX0_SEL_MSK;
		data |= src << CPU_MUX0_SEL_POS;
		set_bit(CPU_MUX0_EN_POS, &data);
		clear_bit(CPU_MUL_SEL_POS, &data);
	}
	else {
		data &= CPU_MUX1_SEL_MSK;
		data |= src << CPU_MUX1_SEL_POS;
		set_bit(CPU_MUX1_EN_POS, &data);
		set_bit(CPU_MUL_SEL_POS, &data);
	}
	writel(data, cpuclk->reg);

	if (lock)
		spin_unlock_irqrestore(lock, flags);

	return;
}

static unsigned long clk_cpu_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct clk_cpu *cpuclk = to_clk_cpu(hwclk);

	return clk_get_rate(cpuclk->source[SRC_PLL0]);
}

static long clk_cpu_round_rate(struct clk_hw *hwclk, unsigned long rate,
			       unsigned long *parent_rate)
{
	struct clk_cpu *cpuclk = to_clk_cpu(hwclk);

	return clk_round_rate(cpuclk->source[SRC_PLL0], rate);
}


static int clk_cpu_set_rate(struct clk_hw *hwclk, unsigned long rate,
			    unsigned long parent_rate)
{
	struct clk_cpu *cpuclk = to_clk_cpu(hwclk);

	/* switch source to sys_clk to avoid cpu gating */
	clk_cpu_manual_switch_source(cpuclk, SRC_SYSCLK);
	clk_set_rate(cpuclk->source[SRC_PLL0], rate);
	clk_cpu_manual_switch_source(cpuclk, SRC_PLL0);
	return 0;
}

static int clk_cpu_prepare(struct clk_hw *hw)
{
	struct clk_cpu *cpuclk = to_clk_cpu(hw);
	unsigned long data;

	data = readl(cpuclk->reg);
	clear_bit(CPU_AUTO_SEL_POS, &data);
	clear_bit(CPU_AUTOMUX_EN_POS, &data);
	writel(data, cpuclk->reg);
	return 0;
}

static const struct clk_ops clk_cpu_ops = {
	.prepare	= clk_cpu_prepare,
	.recalc_rate	= clk_cpu_recalc_rate,
	.round_rate	= clk_cpu_round_rate,
	.set_rate	= clk_cpu_set_rate,
};


struct clk *nufront_clk_register_cpu(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		void __iomem *reg, spinlock_t *lock)
{
	struct clk_cpu *cpuclk;
	struct clk *clk;
	struct clk_init_data init;
	int i;

	pr_debug("%s: %s reg=0x%x %d parents(%s, %s, %s, %s, %s)\n",
		__func__, name, (u32)reg, num_parents,
		parent_names[0], parent_names[1],
		parent_names[2], parent_names[3],
		parent_names[4]);

	cpuclk = kzalloc(sizeof(struct clk_cpu), GFP_KERNEL);
	if (!cpuclk) {
		pr_err("%s: could not allocate divider clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_cpu_ops;
	init.flags = flags | CLK_IS_ROOT | CLK_IS_BASIC;
	init.parent_names = NULL;
	init.num_parents = 0;

	cpuclk->reg = reg;
	cpuclk->flags = 0;
	cpuclk->lock = lock;
	cpuclk->hw.init = &init;

	for (i = 0; i < num_parents; i++) {
		cpuclk->source[i] =
			__clk_lookup(parent_names[i]);
		pr_debug("source %d is %s\n", i, __clk_get_name(cpuclk->source[i]));
	}

	clk = clk_register(dev, &cpuclk->hw);
	if (IS_ERR(clk)) {
		kfree(cpuclk);
	}

	return clk;

}

MODULE_DESCRIPTION("Nufront CPU Clock Driver");
MODULE_AUTHOR("jingyang.wang <jingyang.wang@nufront.com>");
MODULE_LICENSE("GPL");
