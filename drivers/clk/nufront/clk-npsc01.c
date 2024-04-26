/*
 * Copyright 2015 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

//#define DEBUG

#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <dt-bindings/clock/npsc01-clock.h>
#include <linux/io.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include "clk.h"

#define NUFRONT_PRCM_DIRECT_ACCESS
#include <mach/prcm.h>

static struct clk *clk[NPSC01_CLK_MAX_NUM];
static struct clk_onecell_data clk_data;

static const char const *sysclk_24m = "sysclk_24m";

static const char const *pll0 = "pll0";
static const char const *pll1 = "pll1";
static const char const *pll2 = "pll2";
static const char const *pll3 = "pll3";
static const char const *pllddr = "pllddr";

static const char const *clk_1536m = "clk_1536m";
static const char const *clk_768m = "clk_768m";
static const char const *clk_384m = "clk_384m";
static const char const *clk_250m = "clk_250m";
static const char const *clk_192m = "clk_192m";
static const char const *clk_170m = "clk_170m";
static const char const *clk_49m = "clk_49500k";
static const char const *clk_48m = "clk_48m";
static const char const *clk_32m = "clk_32m";
static const char const *clk_12m = "clk_12m";

static const char const *eth0_phy_div = "eth0_phy_div";
static const char const *eth0_phy_clk = "eth0_phy_clk";
static const char const *eth0_div = "eth0_div";
static const char const *eth0_hclk = "eth0_hclk";
static const char const *eth0_clk = "eth0_clk";

static const char const *usb3_host_div = "usb3_host_div";
static const char const *usb3_dev_div = "usb3_dev_div";
static const char const *usb_phy_div = "usb_phy_div";
static const char const *usb_bus_div = "usb_bus_div";

static const char const *usb3_host_suspend = "usb3_host_suspend";
static const char const *usb3_host_aclk = "usb3_host_aclk";
static const char const *usb3_host_clk = "usb3_host_clk";

static const char const *usb3_dev_suspend = "usb3_dev_suspend";
static const char const *usb3_dev_aclk = "usb3_dev_aclk";
static const char const *usb3_dev_clk = "usb3_dev_clk";

static const char const *usb2_host0_clk = "usb2_host0_clk";
static const char const *usb2_host1_clk = "usb2_host1_clk";
static const char const *usb2_dev_clk = "usb2_dev_clk";

static const char const *usb_host0_phy = "usb_host0_phy";
static const char const *usb_host1_phy = "usb_host1_phy";
static const char const *usb_dev_phy = "usb_dev_phy";

static const char const *mali_div0 = "mali_div0";
static const char const *mali_div1 = "mali_div1";
static const char const *mali_clk_sel = "mali_clk_sel";
static const char const *mali_aclk = "mali_aclk";

static const char const *pxl0_div = "pxl0_div";
static const char const *lcdc_aclk = "lcdc_aclk";
static const char const *pxl0_clk = "pxl0_clk";

static const char const *ssi_div = "ssi_div";
static const char const *ssi0_pclk = "ssi0_pclk";
static const char const *ssi1_pclk = "ssi1_pclk";
static const char const *ssi0_clk = "ssi0_clk";
static const char const *ssi1_clk = "ssi1_clk";

static const char const *disp_div = "disp_div";
static const char const *disp_pclk = "disp_pclk";
static const char const *disp_mclk = "disp_mclk";

static const char const *lvds_tx_pclk = "lvds_tx_pclk";

static const char const *i2c0_div = "i2c0_div";
static const char const *i2c1_div = "i2c1_div";
static const char const *i2c0_pclk = "i2c0_pclk";
static const char const *i2c1_pclk = "i2c1_pclk";
static const char const *i2c0_clk = "i2c0_clk";
static const char const *i2c1_clk = "i2c1_clk";

static const char const *uart0_div = "uart0_div";
static const char const *uart1_div = "uart1_div";
static const char const *uart2_div = "uart2_div";
static const char const *uart3_div = "uart3_div";
static const char const *uart0_pclk = "uart0_pclk";
static const char const *uart1_pclk = "uart1_pclk";
static const char const *uart2_pclk = "uart2_pclk";
static const char const *uart3_pclk = "uart3_pclk";
static const char const *uart0_clk = "uart0_clk";
static const char const *uart1_clk = "uart1_clk";
static const char const *uart2_clk = "uart2_clk";
static const char const *uart3_clk = "uart3_clk";

static const char const *sd0_div = "sd0_div";
static const char const *sd2_div = "sd2_div";
static const char const *sd0_hclk = "sd0_hclk";
static const char const *sd2_hclk = "sd2_hclk";
static const char const *sd0_clk = "sd0_clk";
static const char const *sd2_clk = "sd2_clk";

static const char const *efuse_pclk = "efuse_pclk";
static const char const *efuse_clk = "efuse_clk";

static const char const *ccp_pclk = "ccp_pclk";
static const char const *ccp_clk = "ccp_clk";

static const char const *i2s0_div = "i2s0_div";
static const char const *i2s0_pclk = "i2s0_pclk";
static const char const *i2s0_clk = "i2s0_clk";

static const char const *i2s_div = "i2s_div";
static const char const *i2s_pclk = "i2s_pclk";
static const char const *i2s_clk = "i2s_clk";

static const char const *cpu_clk_parent[] = { "pll0", "cpu_div0", "pllddr", "cpu_div1", "sysclk_24m"};
static const char const *mali_clk_parent[] = {"mali_div0", "mali_div1"};

static const char const *pwm0_pclk = "pwm0_pclk";
static const char const *pwm1_pclk = "pwm1_pclk";
static const char const *pwm0_clk = "pwm0_clk";
static const char const *pwm1_clk = "pwm1_clk";

static const char const *wdt_pclk = "wdt_pclk";
static const char const *wdt_clk = "wdt_clk";

static const char const *cpu_div0 = "cpu_div0";
static const char const *cpu_div1 = "cpu_div1";
static const char const *cpu_clk = "cpu_clk";
static const char const *bus_div = "bus_div";
static const char const *scu_periph = "scu_periph";

static DEFINE_SPINLOCK(npsc01_clk_lock);

static struct clk_pll_table pll_table[] = {
	PLL_PARAMS(24*MHZ, 1, 1, 1, 1),
	{ 0 }
};

static struct clk_div_table bus_div_table[] = {
	{ .div = 1, .val = 0 },
	{ .div = 2, .val = 1 },
	{ .div = 3, .val = 2 },
	{ .div = 4, .val = 3 },
	{ .div = 0 },
};

static void display_clk_info(struct clk *clk)
{
	clk_prepare(clk);

	pr_info("clk %s: rate [%u].\n",
		__clk_get_name(clk),
		(u32)clk_get_rate(clk));

	clk_unprepare(clk);
}

static void dump_all_clks(void)
{
	int i;
	for (i=0; i<ARRAY_SIZE(clk); i++) {

		if(!clk[i])
			continue;

		if (IS_ERR(clk[i])) {
			printk("scanning error at clock %d.\n", i);
			continue;
		}

		display_clk_info(clk[i]);
	}
}

static void __init npsc01_clocks_init(struct device_node *np)
{
	spinlock_t *_lock = &npsc01_clk_lock;

	if (!get_prcm_base()) {
		printk("%s error!\n", __func__);
		return;
	}

	clk_register_fixed_rate(NULL, sysclk_24m, NULL, CLK_IS_ROOT, 24*MHZ);

	nufront_clk_pll_default(pll0, sysclk_24m, PRCM_PLL0_CFG0, PRCM_SYS_PLL_CTRL, 0, pll_table);
	nufront_clk_pll_default(pll1, sysclk_24m, PRCM_PLL1_CFG0, PRCM_SYS_PLL_CTRL, 1, pll_table);
	nufront_clk_pll_default(pll2, sysclk_24m, PRCM_PLL2_CFG0, PRCM_SYS_PLL_CTRL, 2, pll_table);
	nufront_clk_pll_default(pll3, sysclk_24m, PRCM_PLL3_CFG0, PRCM_SYS_PLL_CTRL, 3, pll_table);
	nufront_clk_pll_default(pllddr, sysclk_24m, PRCM_DDR_PLL_CFG0, PRCM_SYS_PLL_CTRL, 8, pll_table);

	clk_register_fixed_rate(NULL, clk_1536m, NULL, CLK_IS_ROOT, 1536*MHZ);
	clk_register_fixed_rate(NULL, clk_768m, NULL, CLK_IS_ROOT, 768*MHZ);
	clk_register_fixed_rate(NULL, clk_384m, NULL, CLK_IS_ROOT, 384*MHZ);
	clk_register_fixed_rate(NULL, clk_250m, NULL, CLK_IS_ROOT, 250*MHZ);
	clk_register_fixed_rate(NULL, clk_192m, NULL, CLK_IS_ROOT, 192*MHZ);
	nufront_clk_fixed_factor_default(clk_170m, clk_1536m, 1, 9); /* pll2_bus_root_clk -> [1/9] -> 170.7 */
	nufront_clk_fixed_factor_default(clk_49m, clk_1536m, 1, 31); /* pll2_bus_root_clk -> [1/31] -> 49.5 */
	nufront_clk_fixed_factor_default(clk_48m, clk_384m, 1, 8); /* 384 -> [1/8] -> 48 */
	nufront_clk_fixed_factor_default(clk_32m, clk_384m, 1, 12);/* 384 -> [1/12] -> 32 */
	nufront_clk_fixed_factor_default(clk_12m, sysclk_24m, 1, 2); /* sysclk_24m -> [1/2] -> 12 */

	/* ethernet (phy) */
	clk_register_divider(NULL, eth0_phy_div, clk_250m, 0, PRCM_ETHERNET0_CLK_CTRL,
		24, 5, CLK_DIVIDER_ONE_BASED, NULL); /* 5bit[1/N]-> 50 */
	clk[NPSC01_ETH0_PHY_CLK] = nufront_clk_gate_default(eth0_phy_clk, eth0_phy_div, PRCM_ETHERNET0_CLK_CTRL, 29);/* ethernet0_phy_refclk */

	/* ethernet (eth) */
	nufront_clk_divider_default(eth0_div, clk_250m, PRCM_ETHERNET0_CLK_CTRL, 0, 8, 8); /* 8bit[1/N]-> 125 */
	nufront_clk_gate_default(eth0_hclk, eth0_div, PRCM_ETHERNET0_CLK_CTRL, 10);/* ethernet_hclk */
	clk[NPSC01_ETH0_CLK] = nufront_clk_gate_default(eth0_clk, eth0_hclk, PRCM_ETHERNET0_CLK_CTRL, 9);/* ethernet0_clk */

	/* USB (u3\suspend) */
	clk[NPSC01_USB3_HOST_SUSPEND] = nufront_clk_gate(usb3_host_suspend, sysclk_24m,
			PRCM_USB3_CLK_CTRL, 2, CLK_IGNORE_UNUSED); /* usb3_host_suspend_clk */
	clk[NPSC01_USB3_DEV_SUSPEND] = nufront_clk_gate(usb3_dev_suspend, sysclk_24m,
			PRCM_USB3_CLK_CTRL, 7, CLK_IGNORE_UNUSED); /* usb3_dev_suspend_clk */

	/* USB (usb\divider) */
	clk_register_divider(NULL, usb3_host_div, clk_250m, 0, PRCM_USB3_CLK_CTRL,
			20, 5, CLK_DIVIDER_ONE_BASED, NULL); /* 5bit[1/N]-> 125 */
	clk_register_divider(NULL, usb3_dev_div, clk_250m, 0, PRCM_USB3_CLK_CTRL,
			25, 5, CLK_DIVIDER_ONE_BASED, NULL); /* 5bit[1/N]-> 125 */
	nufront_clk_divider_default(usb_bus_div, clk_250m, PRCM_USB_CLK_CTRL, 16, 4, 11); /* 4bit[1/N]-> 125 */
	nufront_clk_divider_default(usb_phy_div, clk_250m, PRCM_USB_CLK_CTRL, 12, 4, 10); /* 4bit[1/N]-> 25  */

	/* USB3 (u3 ctrl) */
	nufront_clk_gate_default(usb3_host_aclk, usb3_host_div, PRCM_USB3_CLK_CTRL, 1);/* usb3_host_aclk */
	clk[NPSC01_USB3_HOST_CLK] = nufront_clk_gate_default(usb3_host_clk, usb3_host_aclk, PRCM_USB3_CLK_CTRL, 3);/* usb3_host_refclk */
	nufront_clk_gate_default(usb3_dev_aclk, usb3_dev_div, PRCM_USB3_CLK_CTRL, 6);/* usb3_dev_aclk */;
	clk[NPSC01_USB3_DEV_CLK] = nufront_clk_gate_default(usb3_dev_clk, usb3_dev_aclk, PRCM_USB3_CLK_CTRL, 8);/* usb3_dev_refclk */

	/* USB2 (u2 ctrl) */
	clk[NPSC01_USB2_HOST0_CLK] = nufront_clk_gate_default(usb2_host0_clk, usb_bus_div, PRCM_USB_CLK_CTRL, 2);/* host_hclk SYNC host_ohci_48m_clk */
	clk[NPSC01_USB2_HOST1_CLK] = nufront_clk_gate_default(usb2_host1_clk, usb_bus_div, PRCM_USB_CLK_CTRL, 7);/* host1_hclk SYNC host1_ohci_48m_clk */
	clk[NPSC01_USB2_DEV_CLK] = nufront_clk_gate_default(usb2_dev_clk, usb_bus_div, PRCM_USB_CLK_CTRL, 3);/* otg_hclk */

	/* USB PHY (usb phy) */
	clk[NPSC01_USB_HOST0_PHY] = nufront_clk_gate_default(usb_host0_phy, usb_phy_div, PRCM_USB_CLK_CTRL, 0);/* host_ref_clk */
	clk[NPSC01_USB_HOST1_PHY] = nufront_clk_gate(usb_host1_phy, clk_12m, PRCM_USB_CLK_CTRL, 8, 0);/* host1_ref_clk */
	clk[NPSC01_USB_DEV_PHY] = nufront_clk_gate_default(usb_dev_phy, usb_phy_div, PRCM_USB_CLK_CTRL, 1);/* otg_ref_clk */

	/* MALI (mali) */
	clk[NPSC01_MALI_DIV0] = nufront_clk_divider_default(mali_div0, clk_768m, PRCM_MALI_CLK_CTRL, 0, 4, 4); /* [1/N]-> 384 */
	clk[NPSC01_MALI_DIV1] = nufront_clk_fixed_factor_default(mali_div1, clk_1536m, 1, 5); /* [1/5]-> 307 */
	clk[NPSC01_MALI_SEL] = nufront_clk_mux_default(mali_clk_sel, mali_clk_parent, ARRAY_SIZE(mali_clk_parent),
		PRCM_MALI_CLK_CTRL, 16, 1);
	clk[NPSC01_MALI_ACLK] = nufront_clk_gate_default(mali_aclk, mali_clk_sel, PRCM_MALI_CLK_CTRL, 17);/* mali_aclk */

	/* LCDC (lcdc\pixel-clock) */
	nufront_clk_divider_default(pxl0_div, clk_384m, PRCM_DIS0_CLK_CTRL, 0, 5, 5); /* 384 -> 5bit[1/N] -> 76.8 */
	clk[NPSC01_PXL0_CLK] = nufront_clk_gate_default(pxl0_clk, pxl0_div, PRCM_DIS0_CLK_CTRL, 8); /* Pxl0_clk */
	nufront_clk_divider_default(disp_div, clk_384m, PRCM_DIS0_CLK_CTRL, 20, 4, 24); /* 4bit [1/N] 96/... */
	nufront_clk_gate_default(disp_mclk, disp_div, PRCM_DIS0_CLK_CTRL, 11); /* disp0_mclk */
	nufront_clk_gate_default(lcdc_aclk, disp_mclk, PRCM_DIS0_CLK_CTRL, 9); /* lcdc0_aclk */
	clk[NPSC01_DISP0_CLK] = nufront_clk_gate_default(disp_pclk, lcdc_aclk, PRCM_DIS0_CLK_CTRL, 12); /* disp0_pclk */

	/* LVDS phy (LCDC\lvds) */
	clk[NPSC01_LVDS_CLK] = nufront_clk_gate(lvds_tx_pclk, clk_49m, PRCM_LVDS_CLK_CTRL, 0, 0); /* lvds_tx_pclk */

	/* ssi (SPI\spi) */
	nufront_clk_divider_default(ssi_div, clk_384m, PRCM_SPI_CLK_CTRL, 0, 4, 4);/* 384 -> 4bit[1/N] -> 54.9 */
	nufront_clk_gate_default(ssi0_pclk, ssi_div, PRCM_SPI_CLK_CTRL, 10); /* ssi_pclk */
	nufront_clk_gate_default(ssi1_pclk, ssi_div, PRCM_SPI_CLK_CTRL, 11); /* ssi_pclk */
	clk[NPSC01_SSI0_CLK] = nufront_clk_gate_default(ssi0_clk, ssi0_pclk, PRCM_SPI_CLK_CTRL, 8); /* ssi_clk */
	clk[NPSC01_SSI1_CLK] = nufront_clk_gate_default(ssi1_clk, ssi1_pclk, PRCM_SPI_CLK_CTRL, 9); /* ssi_clk */

	/* I2C (i2c) */
	nufront_clk_divider_default(i2c0_div, clk_384m, PRCM_I2C_CLK_CTRL0, 0, 4, 4); /* 384 -> 4bit[1/N] -> 192 */
	nufront_clk_divider_default(i2c1_div, clk_384m, PRCM_I2C_CLK_CTRL0, 8, 4, 12); /* 384 -> 4bit[1/N] -> 192 */
	nufront_clk_gate_default(i2c0_pclk, i2c0_div, PRCM_I2C_CLK_CTRL0, 6); /* I2c_pclk */
	nufront_clk_gate_default(i2c1_pclk, i2c1_div, PRCM_I2C_CLK_CTRL0, 14); /* I2c_pclk */
	clk[NPSC01_I2C0] = nufront_clk_gate_default(i2c0_clk, i2c0_pclk, PRCM_I2C_CLK_CTRL0, 5); /* I2c_clk */
	clk[NPSC01_I2C1] = nufront_clk_gate_default(i2c1_clk, i2c1_pclk, PRCM_I2C_CLK_CTRL0, 13); /* I2c_clk */

	/* UART (uart\serial) */
	nufront_clk_divider_default(uart0_div, clk_384m, PRCM_UART_CLK_CTRL0, 0, 4, 4); /* 384 -> 4bit[1/N] -> 64 */
	nufront_clk_divider_default(uart1_div, clk_384m, PRCM_UART_CLK_CTRL0, 8, 4, 12); /* 384 -> 4bit[1/N] -> 64 */
	nufront_clk_divider_default(uart2_div, clk_384m, PRCM_UART_CLK_CTRL0, 16, 4, 20); /* 384 -> 4bit[1/N] -> 64 */
	nufront_clk_divider_default(uart3_div, clk_384m, PRCM_UART_CLK_CTRL0, 24, 4, 28); /* 384 -> 4bit[1/N] -> 64 */
	nufront_clk_gate_default(uart0_pclk, uart0_div, PRCM_UART_CLK_CTRL1, 5); /* uart_pclk */
	nufront_clk_gate_default(uart1_pclk, uart1_div, PRCM_UART_CLK_CTRL1, 6); /* uart_pclk */
	nufront_clk_gate_default(uart2_pclk, uart2_div, PRCM_UART_CLK_CTRL1, 7); /* uart_pclk */
	nufront_clk_gate_default(uart3_pclk, uart3_div, PRCM_UART_CLK_CTRL1, 8); /* uart_pclk */
	clk[NPSC01_UART0_CLK] = nufront_clk_gate_default(uart0_clk, uart0_pclk, PRCM_UART_CLK_CTRL1, 0); /* uart_clk */
	clk[NPSC01_UART1_CLK] = nufront_clk_gate_default(uart1_clk, uart1_pclk, PRCM_UART_CLK_CTRL1, 1); /* uart_clk */
	clk[NPSC01_UART2_CLK] = nufront_clk_gate_default(uart2_clk, uart2_pclk, PRCM_UART_CLK_CTRL1, 2); /* uart_clk */
	clk[NPSC01_UART3_CLK] = nufront_clk_gate_default(uart3_clk, uart3_pclk, PRCM_UART_CLK_CTRL1, 3); /* uart_clk */

	/* SD (sd\EMMC\emmc) */
	nufront_clk_divider_default(sd0_div, clk_384m, PRCM_SD_CLK_CTRL, 0, 4, 4); /* 384 -> 4bit[1/N] -> 96 */
	nufront_clk_divider_default(sd2_div, clk_384m, PRCM_SD_CLK_CTRL, 16, 4, 20); /* 384 -> 4bit[1/N] -> 96 */
	nufront_clk_gate_default(sd0_hclk, sd0_div, PRCM_SD_CLK_CTRL, 6); /* sd_hclk */
	nufront_clk_gate_default(sd2_hclk, sd2_div, PRCM_SD_CLK_CTRL, 22); /* sd_hclk */
	clk[NPSC01_SD0_CLK] = nufront_clk_gate_default(sd0_clk, sd0_hclk, PRCM_SD_CLK_CTRL, 5); /* sd_clk */
	clk[NPSC01_SD2_CLK] = nufront_clk_gate_default(sd2_clk, sd2_hclk, PRCM_SD_CLK_CTRL, 21); /* sd_clk */

	/* efuse */
	nufront_clk_gate(efuse_pclk, clk_32m, PRCM_EFUSE_CLK_CTRL, 1, 0); /* efuse_pclk */
	clk[NPSC01_EFUSE_CLK] = nufront_clk_gate(efuse_clk, efuse_pclk, PRCM_EFUSE_CLK_CTRL, 0, 0); /* efuse_clk */

	/* I2S (i2s) */
	nufront_clk_divider_default(i2s_div, clk_384m, PRCM_AUDIO_CLK_CTRL, 0, 5, 8);/* [1/n]5bit-> 128 */
	nufront_clk_gate_default(i2s_pclk, i2s_div, PRCM_AUDIO_CLK_CTRL, 18); /* i2s_pclk */
	clk[NPSC01_I2S0_CLK] = nufront_clk_gate_default(i2s_clk, i2s_pclk, PRCM_AUDIO_CLK_CTRL, 16); /* i2s0_clk */

	/* CCP ccp/ccs */
	nufront_clk_gate(ccp_pclk, clk_170m, PRCM_CCPCCS_CLK_CTRL, 0, 0); /* ccp_pclk */
	clk[NPSC01_CCP_CLK] = nufront_clk_gate(ccp_clk, ccp_pclk, PRCM_CCPCCS_CLK_CTRL, 0, 0); /* ccp_aclk */

	/* I2S (i2s) */
	nufront_clk_divider_default(i2s0_div, clk_384m, PRCM_AUDIO_CLK_CTRL, 0, 5, 8); /* audio_div_clk */
	nufront_clk_gate_default(i2s0_pclk, i2s0_div, PRCM_AUDIO_CLK_CTRL, 0); /* i2s_pclk */
	clk[NPSC01_I2S0_CLK] = nufront_clk_gate_default(i2s0_clk, i2s0_pclk, PRCM_AUDIO_CLK_CTRL, 0); /* i2s_pclk */

	/* PWM (pwm) */
	nufront_clk_gate(pwm0_pclk, sysclk_24m, PRCM_PWM_CLK_CTRL, 1, 0); /* pwm0_pclk */
	nufront_clk_gate(pwm1_pclk, sysclk_24m, PRCM_PWM_CLK_CTRL, 4, 0); /* pwm0_pclk */
	clk[NPSC01_PWM0_CLK] = nufront_clk_gate(pwm0_clk, sysclk_24m, PRCM_PWM_CLK_CTRL, 0, 0); /* pwm0_clk */
	clk[NPSC01_PWM1_CLK] = nufront_clk_gate(pwm1_clk, sysclk_24m, PRCM_PWM_CLK_CTRL, 3, 0); /* pwm0_clk */

	/* WDT (wdt) */
	nufront_clk_gate(wdt_pclk, sysclk_24m, PRCM_WDT_CLK_CTRL, 1, 0); /* wdt_pclk */
	clk[NPSC01_WDT_CLK] = nufront_clk_gate(wdt_clk, wdt_pclk, PRCM_WDT_CLK_CTRL, 0, 0); /* wdt_clk */

	/* CPU (cpu)*/
	nufront_clk_divider_default(cpu_div0, pll0, PRCM_CPU_CLK_DIV, 0, 5, 15);
	nufront_clk_divider_default(cpu_div1, pllddr, PRCM_CPU_CLK_DIV, 8, 5, 7);
	clk[NPSC01_CPU_CLK] = nufront_clk_register_cpu(NULL, cpu_clk,
		cpu_clk_parent, ARRAY_SIZE(cpu_clk_parent),
		0, PRCM_CPU_CLK_SWITCH, NULL);
	clk[NPSC01_BUS_CLK] = nufront_clk_register_divider_table(
		NULL, bus_div,
		cpu_clk, 0,
		PRCM_CPU_CLK_DIV, 16, 2, 20,
		CLK_DIVIDER_ALLOW_ZERO, bus_div_table,
		_lock);

	clk[NPSC01_SCU_PERIPH_CLK] = nufront_clk_fixed_factor_default(
		scu_periph, cpu_clk, 1, 2); /* CPU -> [1/2] -> SCU_PERIPHCLK */

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

#ifdef DEBUG
	dump_all_clks();
#endif

	/* Setup default stat */
	clk_prepare_enable(clk[NPSC01_UART0_CLK]);
	return;
}

CLK_OF_DECLARE(npsc01, "nufront,clks", npsc01_clocks_init);
