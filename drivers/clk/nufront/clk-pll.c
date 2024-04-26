/*
 * Copyright 2015 Nufront Corporation.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/module.h>

#include "clk.h"
#include "q16math.h"
#include "clk-reset.h"

struct clk_pll {
	struct clk_hw   hw;
	void __iomem *reg;
	u8 flags;
	const struct clk_pll_table *table;
	struct clk_pll_table cached;
	spinlock_t *lock;
	struct clk_reset reset;
};

#define to_clk_pll(_hw) container_of(_hw, struct clk_pll, hw)

/*==============================================================*/
#define VCO_FREQ_MAX (2600*MHZ)
#define VCO_FREQ_MIN (600*MHZ)

#define DSMPD (1<<26)
#define DACPD (1<<25)

#define FBDIV_MASK 0xfff
#define FBDIV_SHIFT 0
#define FBDIV_LIMIT (FBDIV_MASK>>FBDIV_SHIFT)

#define REFDIV_MASK 0x3f000
#define REFDIV_SHIFT 12
#define REFDIV_LIMIT (REFDIV_MASK>>REFDIV_SHIFT)

#define POSTDIV1_MASK 0x1C0000
#define POSTDIV1_SHIFT 18
#define POSTDIV1_LIMIT (POSTDIV1_MASK>>POSTDIV1_SHIFT)

#define POSTDIV2_MASK 0xE00000
#define POSTDIV2_SHIFT 21
#define POSTDIV2_LIMIT (POSTDIV2_MASK>>POSTDIV2_SHIFT)

/*==============================================================*/
/* Post divider  */

#define POSTDIV_MAKE(div1, div2) (div1<<16 | div2)

/*
 *  value = div1<<16 | div2
 *  index = div1 * div2
*/
static const u32 _postdiv_table[] = {
	0, /* 0 */
	POSTDIV_MAKE(1, 1), /* 1 */
	POSTDIV_MAKE(2, 1), /* 2 */
	POSTDIV_MAKE(3, 1), /* 3 */
	POSTDIV_MAKE(4, 1), /* 4 */
	POSTDIV_MAKE(5, 1), /* 5 */
	POSTDIV_MAKE(6, 1), /* 6 */
	POSTDIV_MAKE(7, 1), /* 7 */
	POSTDIV_MAKE(4, 2), /* 8 */
	POSTDIV_MAKE(3, 3), /* 9 */
	POSTDIV_MAKE(5, 2), /* 10 */
	0, /* 11 */
	POSTDIV_MAKE(6, 2), /* 12 */
	0, /* 13 */
	POSTDIV_MAKE(7, 2), /* 14 */
	POSTDIV_MAKE(5, 3), /* 15 */
	POSTDIV_MAKE(4, 4), /* 16 */
	0, /* 17 */
	POSTDIV_MAKE(6, 3), /* 18 */
	0, /* 19 */
	POSTDIV_MAKE(5, 4), /* 20 */
	POSTDIV_MAKE(7, 3), /* 21 */
	0, /* 22 */
	0, /* 23 */
	POSTDIV_MAKE(6, 4), /* 24 */
	POSTDIV_MAKE(5, 5), /* 25 */
	0, /* 26 */
	0, /* 27 */
	POSTDIV_MAKE(7, 4), /* 28 */
	0, /* 29 */
	POSTDIV_MAKE(6, 5), /* 30 */
	0, /* 31 */
	0, /* 32 */
	0, /* 33 */
	0, /* 34 */
	POSTDIV_MAKE(7, 5), /* 35 */
	POSTDIV_MAKE(6, 6), /* 36 */
	0, /* 37 */
	0, /* 38 */
	0, /* 39 */
	0, /* 40 */
	0, /* 41 */
	POSTDIV_MAKE(7, 6), /* 42 */
	0, /* 43 */
	0, /* 44 */
	0, /* 45 */
	0, /* 46 */
	0, /* 47 */
	0, /* 48 */
	POSTDIV_MAKE(7, 7), /* 49 */
};

#define POSTDIV_MAX (ARRAY_SIZE(_postdiv_table)-1)
#define POSTDIV_VALID(div) (_postdiv_table[div])
#define POSTDIV_GET_DIV1(div) (_postdiv_table[div] >> 16)
#define POSTDIV_GET_DIV2(div) (_postdiv_table[div] & 0xffff)

/*==============================================================*/


static unsigned long calc_pll_rate(u32 rate, u32 mul, u32 div0, u32 div1, u32 div2)
{
	return (unsigned long)div_u64((u64)rate * (u64)mul,
		div0 * div1 * div2);
}

static bool pll_vco_check(u32 fin, u32 mul, u32 div0)
{
	u32 vco;

	vco = fin * mul * div0;
	if (vco < VCO_FREQ_MIN || vco > VCO_FREQ_MAX)
		return false;/* out of vco range */

	return true;
}

static int pll_fast_calc_clk(
	u32 fout,
	u32 fin,
	u32 *mul,
	u32 *div0,
	u32 *div1,
	u32 *div2)
{
	u32 remain;
	u32 n;
	u32 div;

	/* Fout / 1Mhz = n */
	n = (u32)div_u64_rem(fout, MHZ, &remain);
	if(remain)
		return -2;

	/* fin / 1Mhz = div */
	div = (u32)div_u64_rem(fin, MHZ, &remain);
	if(remain || div < 1 || div > REFDIV_LIMIT)
		return -3;

	if (!pll_vco_check(fin, n, div))
		return -1;

	*mul = n;
	*div0 = div;
	*div1 = 1;
	*div2 = 1;
	return 0;
}

/*
 *  DSMPD = 1 PLL on integer mode
 *
 *  FOUTPOSTDIV = (FREF/REFDIV) x FBDIV/POSTDIV1/POSTDIV2
 *  fout = (fin / div0) * mul / postdiv1 / postdiv2
 *  fout/fin = mul / (div0 * postdiv1 * postdiv2)
 *  fout/fin = factor
 *  mul = factor * div0 * (postdiv1 * postdiv1)
 */
static int pll_calc_best_clk(
	u32 fout,
	u32 fin,
	u32 *mul,
	u32 *div0,
	u32 *div1,
	u32 *div2,
	int flags)
{
	const u32 mul_range = FBDIV_LIMIT;
	const u32 div0_range = REFDIV_LIMIT;
	const u32 div1_range = POSTDIV1_LIMIT;
	const u32 div2_range = POSTDIV2_LIMIT;
	const Q16 bias = MAKE_Q16(0.01f);
	const Q16 mul_max = MAKE_Q16U(mul_range) + bias;
	const Q16 mul_min = Q16_ONE - bias;
	Q16 factor;
	Q16 q_mul;
	u32 u_div0;
	u32 postdiv;

	if(fout < calc_pll_rate(fin, 1, div0_range, div1_range, div2_range) ||
		fout > VCO_FREQ_MAX)
		return -1;

	/* fast calc */
	if(!pll_fast_calc_clk(fout, fin, mul, div0, div1, div2))
		return 0;

	factor = q16_div(fout, fin);

	for(postdiv = 1; postdiv <= POSTDIV_MAX; ++postdiv) {
		if(!POSTDIV_VALID(postdiv))
				continue;

		for(u_div0 = 1; u_div0 <= div0_range; ++u_div0) {
			q_mul = factor * u_div0 * postdiv;

			if (q_mul > mul_max) {
				/* mul already too big */
				break;
			}
			else if(q_mul >= mul_min) {
				if(q16_abs(q_mul - q16_round(q_mul)) < bias) {
					*div0 = u_div0;
					*div1 = POSTDIV_GET_DIV1(postdiv);
					*div2 = POSTDIV_GET_DIV2(postdiv);
					*mul = q16_to_uint(q_mul);
					if (pll_vco_check(fin, *mul, *div0)) {
						return 0;
					}
				}
			}
		}
	}
	pr_err("%s: fail to caculate best frequency. \n", __func__);
	return -3;
}

static int clk_pll_get_best_div_mul(
	struct clk_pll *pll,
	u32 target_rate,
	u32 parent_rate,
	u32 *mul,
	u32 *div0,
	u32 *div1,
	u32 *div2)
{
	const struct clk_pll_table *clkt;
	int err;

	/* check cache */
	if(pll->cached.want_rate == target_rate ||
		pll->cached.rate == target_rate) {
		*div0 = pll->cached.div0;
		*div1 = pll->cached.div1;
		*div2 = pll->cached.div2;
		*mul = pll->cached.mul;
		return 0;
	}

	/* lookup table */
	for (clkt = pll->table; clkt && clkt->rate; clkt++) {
		if(clkt->rate == target_rate) {
			*div0 = clkt->div0;
			*div1 = clkt->div1;
			*div2 = clkt->div2;
			*mul = clkt->mul;
			return 0;
		}
	}

	/* calc new */
	err = pll_calc_best_clk(
						target_rate, parent_rate,
						mul, div0, div1, div2, pll->flags);
	/* refresh cached param */
	if(err == 0) {
		pll->cached.want_rate = target_rate;
		pll->cached.rate =
			calc_pll_rate(parent_rate, *mul, *div0, *div1, *div2);
		pll->cached.div0 = *div0;
		pll->cached.div1 = *div1;
		pll->cached.div2 = *div2;
		pll->cached.mul = *mul;
	}

	return err;
}

static void clk_set_pll_reg(struct clk_pll *pll,
	u32 mul,
	u32 div0,
	u32 div1,
	u32 div2)
{
	unsigned int data = 0;
	unsigned long flags = 0;

	if (pll->lock) {
		WARN_ON(spin_is_locked(pll->lock));
		spin_lock_irqsave(pll->lock, flags);
	}

	data = DSMPD;
	data |= mul<<FBDIV_SHIFT;
	data |= div0<<REFDIV_SHIFT;
	data |= div1<<POSTDIV1_SHIFT;
	data |= div2<<POSTDIV2_SHIFT;
	writel(data, pll->reg);

	clk_reset_assert(&pll->reset);
	udelay(10);
	clk_reset_deassert(&pll->reset);

	if (pll->lock)
		spin_unlock_irqrestore(pll->lock, flags);
}

static void clk_get_pll_reg(struct clk_pll *pll,
	u32 *mul,
	u32 *div0,
	u32 *div1,
	u32 *div2)
{
	u32 reg_value;

	if (pll->lock)
		spin_unlock_wait(pll->lock);

	reg_value = readl(pll->reg);
	*mul = (reg_value & FBDIV_MASK) >> FBDIV_SHIFT;
	*div0 = (reg_value & REFDIV_MASK) >> REFDIV_SHIFT;
	*div1 = (reg_value & POSTDIV1_MASK) >> POSTDIV1_SHIFT;
	*div2 = (reg_value & POSTDIV2_MASK) >> POSTDIV2_SHIFT;
}


static int clk_pll_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 mul;
	u32 div0;
	u32 div1;
	u32 div2;
	int err = 0;

	err = clk_pll_get_best_div_mul(pll, rate, parent_rate,
		&mul, &div0, &div1, &div2);
	if(err != 0) {
		pr_err("calc rate error %d. target rate is %u\n", err, (u32)rate);
		return -EINVAL;
	}
	clk_set_pll_reg(pll, mul, div0, div1, div2);
	return 0;
}

static long clk_pll_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 mul, div0, div1, div2;
	int err;

	err = clk_pll_get_best_div_mul(pll, rate, *parent_rate,
		&mul, &div0, &div1, &div2);
	if(err != 0) {
		pr_err("calc rate error %d. target rate is %u\n", err, (u32)rate);
		return -EINVAL;
	}
	return (long)calc_pll_rate(*parent_rate, mul, div0, div1, div2);
}

static unsigned long clk_pll_recalc_rate(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	struct clk_pll *pll = to_clk_pll(hw);
	u32 mul, div0, div1, div2;

	clk_get_pll_reg(pll, &mul, &div0, &div1, &div2);
	return (unsigned long)calc_pll_rate(parent_rate, mul, div0, div1, div2);
}

static const struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.round_rate = clk_pll_round_rate,
	.set_rate = clk_pll_set_rate,
};

struct clk *nufront_clk_register_pll(
	struct device *dev, const char *name,
	const char *parent_name, int flags,
	void __iomem *reg,
	void __iomem *rst_reg, u8 rst_bit,
	u8 clk_pll_flags,
	const struct clk_pll_table *table, spinlock_t *lock)
{
	struct clk_pll *pll;
	struct clk *clk;
	struct clk_init_data init;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return ERR_PTR(-ENOMEM);

	pll->reg = reg;
	pll->flags = clk_pll_flags;
	pll->table = table;
	pll->lock = lock;
	clk_reset_init(&pll->reset, rst_reg, rst_bit, true);

	init.name = name;
	init.ops = &clk_pll_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	pll->hw.init = &init;

	clk = clk_register(dev, &pll->hw);
	if (IS_ERR(clk))
		kfree(pll);

	return clk;
}

MODULE_DESCRIPTION("Nufront PLL Clock Driver");
MODULE_AUTHOR("jingyang.wang <jingyang.wang@nufront.com>");
MODULE_LICENSE("GPL");
