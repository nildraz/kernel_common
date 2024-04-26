/*
 * Copyright 2015 Nufront Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __PRCM_NPSC01_H__
#define __PRCM_NPSC01_H__

#define PRCM_SYS_CLK_CTRL           (PRCM_BASE + 0x00)
#define PRCM_SYS_RST_CTRL           (PRCM_BASE + 0x04)

#define PRCM_SYS_MODE               (PRCM_BASE + 0x30)
#define PRCM_WAKEUP_IRQ_STATUS      (PRCM_BASE + 0x3C)
#define PRCM_SYS_PWR_CTRL           (PRCM_BASE + 0x40)

#define PRCM_SYS_PWR_STATUS         (PRCM_BASE + 0x44)
#define PRCM_NUSTAR_PWR_CTRL        (PRCM_BASE + 0x48)
#define PRCM_NUSTAR_PWR_STATUS      (PRCM_BASE + 0x4C)

#define PRCM_WAKEUP_IRQ_CTRL1       (PRCM_BASE + 0x50)
#define PRCM_WAKEUP_IRQ_CTRL2       (PRCM_BASE + 0x54)

#define PRCM_MISC_REG0              (PRCM_BASE + 0x58)
#define PRCM_MISC_REG1              (PRCM_BASE + 0x5C)
#define PRCM_MISC_REG2              (PRCM_BASE + 0x60)
#define PRCM_MISC_REG3              (PRCM_BASE + 0x64)
#define PRCM_MISC_REG4              (PRCM_BASE + 0x68)
#define PRCM_MISC_REG5              (PRCM_BASE + 0x6C)
#define PRCM_MISC_REG6              (PRCM_BASE + 0x70)
#define PRCM_MISC_REG7              (PRCM_BASE + 0x74)
#define PRCM_MISC_REG8              (PRCM_BASE + 0x78)
#define PRCM_MISC_REG9              (PRCM_BASE + 0x7C)

#define PRCM_SYS_PLL_CTRL           (PRCM_BASE + 0x80)
#define PRCM_SYS_PLL_STATUS         (PRCM_BASE + 0x84)
#define PRCM_PLL0_CFG0              (PRCM_BASE + 0x88)
#define PRCM_PLL1_CFG0              (PRCM_BASE + 0x8C)
#define PRCM_PLL2_CFG0              (PRCM_BASE + 0x90)
#define PRCM_PLL3_CFG0              (PRCM_BASE + 0x94)
#define PRCM_DDR_PLL_CFG0           (PRCM_BASE + 0xA0)

#define PRCM_MISC_REG10             (PRCM_BASE + 0xC0)
#define PRCM_MISC_REG11             (PRCM_BASE + 0xC4)
#define PRCM_MISC_REG12             (PRCM_BASE + 0xC8)
#define PRCM_MISC_REG13             (PRCM_BASE + 0xCC)
#define PRCM_MISC_REG14             (PRCM_BASE + 0xD0)
#define PRCM_MISC_REG15             (PRCM_BASE + 0xD4)
#define PRCM_MISC_REG16             (PRCM_BASE + 0xD8)
#define PRCM_MISC_REG17             (PRCM_BASE + 0xDC)
#define PRCM_MISC_REG18             (PRCM_BASE + 0xE0)
#define PRCM_MISC_REG19             (PRCM_BASE + 0xE4)

#define PRCM_CPU_CLK_DIV            (PRCM_BASE + 0x100)
#define PRCM_CPU_CLK_SWITCH         (PRCM_BASE + 0x104)

#define PRCM_CORESIGHT_CLK_CTRL     (PRCM_BASE + 0x108)
#define PRCM_DDR_CLK_CTRL           (PRCM_BASE + 0x10C)
#define PRCM_BUS_CLK_CTRL           (PRCM_BASE + 0x110)
#define PRCM_DMA_CLK_CTRL           (PRCM_BASE + 0x114)
#define PRCM_CORE_MEM_CLK_CTRL      (PRCM_BASE + 0x118)
#define PRCM_WKUP_MEM_CLK_CTRL      (PRCM_BASE + 0x11C)
#define PRCM_OUT_CLK_CTRL           (PRCM_BASE + 0x120)
#define PRCM_MALI_CLK_CTRL          (PRCM_BASE + 0x140)
#define PRCM_TZPC_WKUP_CLK_CTRL     (PRCM_BASE + 0x144)
#define PRCM_WDT_CLK_CTRL           (PRCM_BASE + 0x148)
#define PRCM_STIMER_WKUP_CLK_CTRL   (PRCM_BASE + 0x14C)
#define PRCM_BUS_LP_ENA             (PRCM_BASE + 0x180)

#define PRCM_SD_CLK_CTRL            (PRCM_BASE + 0x184)
#define PRCM_USB_CLK_CTRL           (PRCM_BASE + 0x188)
#define PRCM_SPI_CLK_CTRL           (PRCM_BASE + 0x190)
#define PRCM_I2C_CLK_CTRL0          (PRCM_BASE + 0x194)
#define PRCM_I2C_CLK_CTRL1          (PRCM_BASE + 0x198)
#define PRCM_UART_CLK_CTRL0         (PRCM_BASE + 0x19C)
#define PRCM_UART_CLK_CTRL1         (PRCM_BASE + 0x1A0)
#define PRCM_PWM_CLK_CTRL           (PRCM_BASE + 0x1A4)
#define PRCM_GPIO_CORE_CLK_CTRL     (PRCM_BASE + 0x1A8)
#define PRCM_GPIO_WKUP_CLK_CTRL     (PRCM_BASE + 0x1AC)
#define PRCM_TIMER_CORE_CLK_CTRL    (PRCM_BASE + 0x1B0)
#define PRCM_TIMER_WKUP_CLK_CTRL    (PRCM_BASE + 0x1B4)
#define PRCM_EFUSE_CLK_CTRL         (PRCM_BASE + 0x1B8)
#define PRCM_ETHERNET0_CLK_CTRL     (PRCM_BASE + 0x1C0)
#define PRCM_EMIF_CLK_CTRL          (PRCM_BASE + 0x1D0)
#define PRCM_ADC_CLK_CTRL           (PRCM_BASE + 0x1D4)
#define PRCM_LVDS_CLK_CTRL          (PRCM_BASE + 0x1D8)

#define PRCM_USB3_CLK_CTRL          (PRCM_BASE + 0x1E0)
#define PRCM_USB3_MISC_CTRL         (PRCM_BASE + 0x1F4)

#define PRCM_AUDIO_CLK_CTRL         (PRCM_BASE + 0x200)
#define PRCM_DIS0_CLK_CTRL          (PRCM_BASE + 0x204)
#define PRCM_DIS0_CLK_CTRL1         (PRCM_BASE + 0x208)
#define PRCM_CCPCCS_CLK_CTRL        (PRCM_BASE + 0x20C)
#define PRCM_M3_CTRL                (PRCM_BASE + 0x210)
#define PRCM_BUS_PWR_CTRL           (PRCM_BASE + 0x214)
#define PRCM_BUS_PWR_CTRL1          (PRCM_BASE + 0x218)
#define PRCM_BUS_PWR_STATUS         (PRCM_BASE + 0x21C)
#define PRCM_BUS_TIMEOUT_STATUS     (PRCM_BASE + 0x220)

#define PRCM_BUS_TIMEOUT_CTRL       (PRCM_BASE + 0x224)
#define PRCM_USB3_STATUS            (PRCM_BASE + 0x228)
#define PRCM_RTC_MISC_CTRL          (PRCM_BASE + 0x22C)

#endif /* PRCM_H__ */
