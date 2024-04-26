

#ifndef _CLK_RESET_H_
#define _CLK_RESET_H_

#include <linux/bitops.h>

struct clk_reset {
	void __iomem *reg;
	u8 bit_idx;
	u8 polarity;
};

static inline void clk_reset_init(struct clk_reset *clkrst, void __iomem *reg, u8 bit_idx, u8 polarity)
{
	clkrst->reg = reg;
	clkrst->bit_idx = bit_idx;
	clkrst->polarity = polarity;
}

static inline void clk_reset_common(struct clk_reset *clkrst, int enable)
{
	unsigned long data;
	int set = clkrst->polarity ? 0 : 1;

	set ^= enable;

	data = readl(clkrst->reg);

	if (set)
		set_bit(clkrst->bit_idx, &data);
	else
		clear_bit(clkrst->bit_idx, &data);

	writel(data, clkrst->reg);
}

static inline void clk_reset_assert(struct clk_reset *clkrst)
{
	clk_reset_common(clkrst, true);
}

static inline void clk_reset_deassert(struct clk_reset *clkrst)
{
	clk_reset_common(clkrst, false);
}

static inline void clk_reset_reset(struct clk_reset *clkrst)
{
	clk_reset_assert(clkrst);
	clk_reset_deassert(clkrst);
}

#endif /* _CLK_RESET_H_ */

