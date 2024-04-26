/*
 * Copyright 2015 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __MACH_NUFRONT_CLK_H
#define __MACH_NUFRONT_CLK_H

#include <linux/spinlock.h>
#include <linux/clk-provider.h>

#define KHZ (1000)
#define MHZ (KHZ*1000)

struct clk_pll_table {
	u32 want_rate;
	u32 rate;
	u32 mul;
	u32 div0;
	u32 div1;
	u32 div2;
};

#define PLL_PARAMS(_rate, _mul, _div0, _div1, _div2) \
{							\
	.want_rate = _rate, \
	.rate = _rate,		\
	.mul = _mul,		\
	.div0 = _div0,		\
	.div1 = _div1,		\
	.div2 = _div2,		\
}

struct clk *nufront_clk_register_pll(
	struct device *dev, const char *name,
	const char *parent_name, int flags,
	void __iomem *reg,
	void __iomem *rst_reg, u8 rst_bit,
	u8 clk_pll_flags,
	const struct clk_pll_table *table, spinlock_t *lock);

struct clk *nufront_clk_register_cpu(struct device *dev, const char *name,
		const char **parent_names, u8 num_parents, unsigned long flags,
		void __iomem *reg, spinlock_t *lock);


struct clk *nufront_clk_register_divider_table(
	struct device *dev, const char *name,
	const char *parent_name, unsigned long flags,
	void __iomem *reg, u8 shift, u8 width, u8 en_bit_idx,
	u8 clk_divider_flags, const struct clk_div_table *table,
	spinlock_t *lock);

struct clk *nufront_clk_register_divider(
	struct device *dev, const char *name,
	const char *parent_name, unsigned long flags,
	void __iomem *reg, u8 shift, u8 width, u8 en_bit_idx,
	u8 clk_divider_flags, spinlock_t *lock);


#define nufront_clk_pll_default(name, parent_name, reg, rst_reg, rst_bit, table) \
	nufront_clk_register_pll(NULL, name, parent_name, 0, reg, rst_reg, rst_bit, 0, table, _lock);

#define nufront_clk_divider(name, parent_name, reg, shift, width, en_bit_idx, flags) \
	nufront_clk_register_divider(NULL, name, \
	parent_name, 0, \
	reg, shift, width, en_bit_idx, \
	flags, NULL)

#define nufront_clk_divider_default(name, parent_name, reg, shift, width, en_bit_idx) \
	nufront_clk_divider(name, parent_name, \
	reg, shift, width, en_bit_idx, \
	CLK_DIVIDER_ONE_BASED|CLK_DIVIDER_ALLOW_ZERO)

#define nufront_clk_mux_default(name,parent_names,num_parents, reg, shift, width) \
	clk_register_mux(NULL, name, \
	parent_names, num_parents, \
	CLK_SET_RATE_PARENT, \
	reg, shift, width, \
	CLK_MUX_INDEX_ONE, NULL)

#define nufront_clk_gate(name, parent_name, reg, bit_idx, flags) \
	clk_register_gate(NULL, name, parent_name, \
		flags | CLK_IGNORE_UNUSED, \
		reg, bit_idx, 0, _lock)

#define nufront_clk_gate_default(name, parent_name, reg, bit_idx) \
	nufront_clk_gate(name, parent_name, reg, bit_idx, \
		CLK_SET_RATE_PARENT)

#define nufront_clk_fixed_factor_default(name, parent_name, mult, div) \
	clk_register_fixed_factor(NULL, name, parent_name, 0, mult, div)

#endif
