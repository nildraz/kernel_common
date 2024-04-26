/*
 * Copyright 2014 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */


#ifndef __NUFRONT_PINCTRL_NPSC01_H
#define __NUFRONT_PINCTRL_NPSC01_H

#include <mach/pinctrl-nufront.h>

/***************************************
 *           Pin Definition            *
 ***************************************/
static const struct pinctrl_pin_desc npsc01_pins[] = {
	PINCTRL_PIN(0, "avi_tg[0]"),
	PINCTRL_PIN(1, "avi_tg[1]"),
	PINCTRL_PIN(2, "avi_tg3"),
	PINCTRL_PIN(3, "avi_tg4"),
	PINCTRL_PIN(4, "avi_tg5"),
	PINCTRL_PIN(5, "avi_tg6"),
	PINCTRL_PIN(6, "avi_tg7"),
	PINCTRL_PIN(7, "avi_tr[0]"),
	PINCTRL_PIN(8, "avi_tr[1]"),
	PINCTRL_PIN(9, "avi_tr[2]"),
	PINCTRL_PIN(10, "avi_tr3"),
	PINCTRL_PIN(11, "avi_tr4"),
	PINCTRL_PIN(12, "avi_tr5"),
	PINCTRL_PIN(13, "avi_tr6"),
	PINCTRL_PIN(14, "avi_afe_sen"),
	PINCTRL_PIN(15, "avi_afe_sdi"),
	PINCTRL_PIN(16, "avi_afe_sck"),
	PINCTRL_PIN(17, "avi_afe_sdi2"),
	PINCTRL_PIN(18, "avi_lsync0"),
	PINCTRL_PIN(19, "avi_lsync1"),
	PINCTRL_PIN(20, "avi_lsync2"),
	PINCTRL_PIN(21, "avi_lsync3"),
	PINCTRL_PIN(22, "avi_psync0"),
	PINCTRL_PIN(23, "avi_psync1"),
	PINCTRL_PIN(24, "avi_psync2"),
	PINCTRL_PIN(25, "avi_psync3"),
	PINCTRL_PIN(26, "avi_vdhsc0"),
	PINCTRL_PIN(27, "avi_vdhsc1"),
	PINCTRL_PIN(28, "avi_vdhsm0"),
	PINCTRL_PIN(29, "avi_vdhsm1"),
	PINCTRL_PIN(30, "avi_vdhsy0"),
	PINCTRL_PIN(31, "avi_vdhsy1"),
	PINCTRL_PIN(32, "avi_vdhsk0"),
	PINCTRL_PIN(33, "avi_vdhsk1"),
	PINCTRL_PIN(34, "avi_syncer_ld"),
	PINCTRL_PIN(35, "avi_syncer_so"),
	PINCTRL_PIN(36, "avi_syncer_sclk"),
	PINCTRL_PIN(37, "avi_syncer_si"),
	PINCTRL_PIN(38, "avi_syncer2_ld"),
	PINCTRL_PIN(39, "avi_syncer2_so"),
	PINCTRL_PIN(40, "avi_syncer2_sclk"),
	PINCTRL_PIN(41, "avi_syncer2_si"),
	PINCTRL_PIN(42, "emmc_d7"),
	PINCTRL_PIN(43, "emmc_d6"),
	PINCTRL_PIN(44, "emmc_d5"),
	PINCTRL_PIN(45, "emmc_d4"),
	PINCTRL_PIN(46, "emmc_d3"),
	PINCTRL_PIN(47, "emmc_d2"),
	PINCTRL_PIN(48, "emmc_d1"),
	PINCTRL_PIN(49, "emmc_d0"),
	PINCTRL_PIN(50, "emmc_cmd"),
	PINCTRL_PIN(51, "emmc_clk"),
	PINCTRL_PIN(52, "emmc_stb"),
	PINCTRL_PIN(53, "avi_vdc0"),
	PINCTRL_PIN(54, "avi_vdc1"),
	PINCTRL_PIN(55, "avi_vdm0"),
	PINCTRL_PIN(56, "avi_vdm1"),
	PINCTRL_PIN(57, "avi_vdy0"),
	PINCTRL_PIN(58, "avi_vdy1"),
	PINCTRL_PIN(59, "avi_vdk0"),
	PINCTRL_PIN(60, "avi_vdk1"),
	PINCTRL_PIN(61, "avi_mtr[7]"),
	PINCTRL_PIN(62, "avi_mtr[6]"),
	PINCTRL_PIN(63, "avi_mtr[5]"),
	PINCTRL_PIN(64, "avi_mtr[4]"),
	PINCTRL_PIN(65, "avi_mtr[3]"),
	PINCTRL_PIN(66, "avi_mtr[2]"),
	PINCTRL_PIN(67, "avi_mtr[1]"),
	PINCTRL_PIN(68, "avi_mtr[0]"),
	PINCTRL_PIN(69, "avi_dcdec[3]"),
	PINCTRL_PIN(70, "avi_dcdec[2]"),
	PINCTRL_PIN(71, "avi_dcdec[1]"),
	PINCTRL_PIN(72, "avi_dcdec[0]"),
	PINCTRL_PIN(73, "avi_pwm[7]"),
	PINCTRL_PIN(74, "avi_pwm[6]"),
	PINCTRL_PIN(75, "avi_pwm[5]"),
	PINCTRL_PIN(76, "avi_pwm[4]"),
	PINCTRL_PIN(77, "avi_pwm[3]"),
	PINCTRL_PIN(78, "avi_pwm[2]"),
	PINCTRL_PIN(79, "avi_pwm[1]"),
	PINCTRL_PIN(80, "avi_pwm[0]"),
	PINCTRL_PIN(81, "avi_ad0[7]"),
	PINCTRL_PIN(82, "avi_ad0[6]"),
	PINCTRL_PIN(83, "avi_ad0[5]"),
	PINCTRL_PIN(84, "avi_ad0[4]"),
	PINCTRL_PIN(85, "avi_ad0[3]"),
	PINCTRL_PIN(86, "avi_ad0[2]"),
	PINCTRL_PIN(87, "avi_ad0[1]"),
	PINCTRL_PIN(88, "avi_ad0[0]"),
	PINCTRL_PIN(89, "i2c0_scl"),
	PINCTRL_PIN(90, "i2c0_sda"),
	PINCTRL_PIN(91, "i2c1_scl"),
	PINCTRL_PIN(92, "i2c1_sda"),
	PINCTRL_PIN(93, "spi0_miso"),
	PINCTRL_PIN(94, "spi0_clk"),
	PINCTRL_PIN(95, "spi0_cs_n"),
	PINCTRL_PIN(96, "spi0_mosi"),
	PINCTRL_PIN(97, "spi1_miso"),
	PINCTRL_PIN(98, "spi1_clk"),
	PINCTRL_PIN(99, "spi1_cs_n"),
	PINCTRL_PIN(100, "spi1_mosi"),
	PINCTRL_PIN(101, "uart0_rxd"),
	PINCTRL_PIN(102, "uart0_txd"),
	PINCTRL_PIN(103, "uart1_rxd"),
	PINCTRL_PIN(104, "uart1_txd"),
	PINCTRL_PIN(105, "uart2_rxd"),
	PINCTRL_PIN(106, "uart2_txd"),
	PINCTRL_PIN(107, "uart3_rxd"),
	PINCTRL_PIN(108, "uart3_txd"),
	PINCTRL_PIN(109, "pwm0"),
	PINCTRL_PIN(110, "tdo"),
	PINCTRL_PIN(111, "tms"),
	PINCTRL_PIN(112, "tdi"),
	PINCTRL_PIN(113, "ntrst"),
	PINCTRL_PIN(114, "tck"),
	PINCTRL_PIN(115, "sd_clk"),
	PINCTRL_PIN(116, "sd_cmd"),
	PINCTRL_PIN(117, "sd_dat[3]"),
	PINCTRL_PIN(118, "sd_dat[2]"),
	PINCTRL_PIN(119, "sd_dat[1]"),
	PINCTRL_PIN(120, "sd_dat[0]"),
	PINCTRL_PIN(121, "sd_cd_n"),
	PINCTRL_PIN(122, "sd_wp"),
	PINCTRL_PIN(123, "sd_psw"),
	PINCTRL_PIN(124, "mac_phy_txd[3]"),
	PINCTRL_PIN(125, "mac_phy_txd[2]"),
	PINCTRL_PIN(126, "mac_phy_txd[1]"),
	PINCTRL_PIN(127, "mac_phy_txd[0]"),
	PINCTRL_PIN(128, "mac_phy_rxd[3]"),
	PINCTRL_PIN(129, "mac_phy_rxd[2]"),
	PINCTRL_PIN(130, "mac_phy_rxd[1]"),
	PINCTRL_PIN(131, "mac_phy_rxd[0]"),
	PINCTRL_PIN(132, "mac_gmii_mdc"),
	PINCTRL_PIN(133, "mac_gmii_mdo"),
	PINCTRL_PIN(134, "mac_phy_rxdv"),
	PINCTRL_PIN(135, "mac_phy_txen"),
	PINCTRL_PIN(136, "mac_phy_refclk"),
	PINCTRL_PIN(137, "mac_ethernet_txclk"),
	PINCTRL_PIN(138, "mac_ethernet_rxclk"),
	PINCTRL_PIN(139, "wakeup_gpio[7]"),
	PINCTRL_PIN(140, "wakeup_gpio[6]"),
	PINCTRL_PIN(141, "wakeup_gpio[5]"),
	PINCTRL_PIN(142, "wakeup_gpio[4]"),
	PINCTRL_PIN(143, "wakeup_gpio[3]"),
	PINCTRL_PIN(144, "u3vbus_detect"),
	PINCTRL_PIN(145, "wakeup2rtc_iso_n"),
	PINCTRL_PIN(146, "pwr_req"),
	PINCTRL_PIN(147, "sys_reset"),
	PINCTRL_PIN(148, "chip_mode"),
	PINCTRL_PIN(149, "wakeup_gpio[2]"),
	PINCTRL_PIN(150, "wakeup_gpio[1]"),
	PINCTRL_PIN(151, "wakeup_gpio[0]"),
	PINCTRL_PIN(152, "sys_24m_xi"),
	PINCTRL_PIN(153, "sys_24m_xo"),
	PINCTRL_PIN(154, "sysclk_32k_xi"),
	PINCTRL_PIN(155, "sysclk_32k_xo"),
	PINCTRL_PIN(156, "rtc_iso_n"),
};


/***************************************
 *          Group Definition           *
 ***************************************/
/* LCDC */
static const unsigned int lcd0_pins[] = {
	0, 26, 27, 28,  1,  2,  3,  4,  5,
	6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24
};
static const unsigned int lcd0_func[] = {
	0,  0,  0,  0,  1,  1,  1,  1,
	1,  1,  1,  1,  1,  1,  1,  1,
	1,  1,  1,  1,	1,  1,  1,  1,
	1,  1,  1,  1
};

/* mmc0 */
static const unsigned int mmc0_pins[] = { 115, 116, 117, 118, 119, 120, 121, 122, 123  };
static const unsigned int mmc0_func[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0};

/* emmc1 8b */
static const unsigned int mmc1_8b_pins[] = { 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };
static const unsigned int mmc1_8b_func[] = { 1,  1,  1,  1,  1,  1,  1,  1,  1,  1 };

/* emmc1 4b */
static const unsigned int mmc1_4b_pins[] = { 46, 47, 48, 49, 50, 51 };
static const unsigned int mmc1_4b_func[] = {  1,  1,  1,  1,  1,  1 };

/* i2c0 */
static const unsigned int i2c0_pins[] = { 89, 90 };
static const unsigned int i2c0_func[] = { 0,  0 };

/* i2c1 */
static const unsigned int i2c1_pins[] = { 91, 92 };
static const unsigned int i2c1_func[] = {  0,  0 };

/* i2s0 */
static const unsigned int i2s0_pins[]    = { 97, 98, 99, 100 };
static const unsigned int i2s0_func[]  = {  2,  2,  2,  2 };

/* spi0 */
static const unsigned int spi0_pins[]    = { 93, 94, 95, 96 };
static const unsigned int spi0_func[]  = {   0,   0,   3,   0 };

/* spi1 */
static const unsigned int spi1_pins[]    = { 97, 98, 99, 100 };
static const unsigned int spi1_func[]  = {   0,   0,   3,   0 };

/* uart0 */
static const unsigned int uart0_pins[]    = { 101, 102 };
static const unsigned int uart0_func[]  = { 0, 0 };

/* uart1 */
static const unsigned int uart1_pins[]    = { 103, 104 };
static const unsigned int uart1_func[]  = { 0, 0 };

/* uart2 */
static const unsigned int uart2_pins[]    = { 105, 106 };
static const unsigned int uart2_func[]  = { 0, 0 };

/* uart3 */
static const unsigned int uart3_pins[]    = { 107, 108 };
static const unsigned int uart3_func[]  = { 0, 0 };

/* pwm0 */
static const unsigned int pwm0_pins[]    = { 109 };
static const unsigned int pwm0_func[]  = { 1 };

/* general gpio */
static const unsigned int gpio_wakeup_pins[] = {
	/* wakeup_gpio[0~7] */
	151,150,149, 143, 142, 141, 140, 139,
};
static const unsigned int gpio_wakeup_func[] = {
	0, 0, 0, 0, 0, 1, 0, 1,
};

static const unsigned int gpio0_pins[] = {
	/* gpio_ext_port0[0~15] */
	108, 107, 106, 105, 104, 103, 100, 97,
	96,  95,  94,  93,  92,  91,  90, 89,
};
static const unsigned int gpio0_func[] = {
	3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3,
};

static const unsigned int gpio1_pins[] = {
	/* gpio_ext_port0[16~31] */
	118, 117,  52,  51,  50, 121, 122, 123,
	120, 119, 115, 116,	98,  99,  49,	48,
};
static const unsigned int gpio1_func[] = {
	3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 2, 2,
};

static const struct nufront_pin_group nufront_pin_groups[] = {
	/* GPIO */
	PIN_GROUP(gpio_wk_grp, gpio_wakeup_pins, gpio_wakeup_func, NULL, NULL),
	PIN_GROUP(gpio0_grp, gpio0_pins, gpio0_func, NULL, NULL),
	PIN_GROUP(gpio1_grp, gpio1_pins, gpio1_func, NULL, NULL),

	/* LCDC */
	PIN_GROUP(lcd0_grp, lcd0_pins, lcd0_func, NULL, NULL),

	/* mmc */
	PIN_GROUP(mmc0_grp, mmc0_pins, mmc0_func, NULL, NULL),
	PIN_GROUP(mmc1_8b_grp, mmc1_8b_pins, mmc1_8b_func, NULL, NULL),
	PIN_GROUP(mmc1_4b_grp, mmc1_4b_pins, mmc1_4b_func, NULL, NULL),

	/* I2C */
	PIN_GROUP(i2c0_grp, i2c0_pins, i2c0_func, NULL, NULL),
	PIN_GROUP(i2c1_grp, i2c1_pins, i2c1_func, NULL, NULL),

	/* I2S */
	PIN_GROUP(i2s0_grp, i2s0_pins, i2s0_func, NULL, NULL),

	/* SPI */
	PIN_GROUP(spi0_grp, spi0_pins, spi0_func, NULL, NULL),
	PIN_GROUP(spi1_grp, spi1_pins, spi1_func, NULL, NULL),

	/* UART */
	PIN_GROUP(uart0_grp, uart0_pins, uart0_func, NULL, NULL),
	PIN_GROUP(uart1_grp, uart1_pins, uart1_func, NULL, NULL),
	PIN_GROUP(uart2_grp, uart2_pins, uart2_func, NULL, NULL),
	PIN_GROUP(uart3_grp, uart3_pins, uart3_func, NULL, NULL),

	/* PWM */
	PIN_GROUP(pwm0_grp, pwm0_pins, pwm0_func, NULL, NULL),
};

/***************************************
 *    Groups & Function Definition     *
 ***************************************/
static const char *lcd0_groups[] = { "lcd0_grp" };

static const char *pwm0_groups[] = { "pwm0_grp" };

static const char *mmc0_groups[] = { "mmc0_grp" };
static const char *mmc1_groups[] = { "mmc1_8b_grp", "mmc1_4b_grp" };

static const char *i2c0_groups[] = { "i2c0_grp" };
static const char *i2c1_groups[] = { "i2c1_grp" };

static const char *i2s0_groups[] = { "i2s0_grp" };

static const char *spi0_groups[] = { "spi0_grp" };
static const char *spi1_groups[] = { "spi1_grp" };

static const char *uart0_groups[] = { "uart0_grp" };
static const char *uart1_groups[] = { "uart1_grp" };
static const char *uart2_groups[] = { "uart2_grp" };
static const char *uart3_groups[] = { "uart3_grp" };

static const char *gpio_grpups[] = { "gpio_wk_grp", "gpio0_grp", "gpio1_grp" };


static const struct nufront_pmx_func nufront_pmx_functions[] = {
	PMX_FUNC(gpio, gpio_grpups),

	PMX_FUNC(lcd0, lcd0_groups),
	PMX_FUNC(pwm0, pwm0_groups),

	PMX_FUNC(i2c0, i2c0_groups),
	PMX_FUNC(i2c1, i2c1_groups),

	PMX_FUNC(i2s0, i2s0_groups),

	PMX_FUNC(spi0, spi0_groups),
	PMX_FUNC(spi1, spi1_groups),

	PMX_FUNC(uart0, uart0_groups),
	PMX_FUNC(uart1, uart1_groups),
	PMX_FUNC(uart2, uart2_groups),
	PMX_FUNC(uart3, uart3_groups),

	PMX_FUNC(mmc0, mmc0_groups),
	PMX_FUNC(mmc1, mmc1_groups),
};

struct nufront_pin_ctrl npsc01_pin_ctrl = {
	.pins = npsc01_pins,
	.nr_pins = ARRAY_SIZE(npsc01_pins),
	.groups = nufront_pin_groups,
	.nr_groups = ARRAY_SIZE(nufront_pin_groups),
	.functions = nufront_pmx_functions,
	.nr_functions = ARRAY_SIZE(nufront_pmx_functions),
};

#endif
