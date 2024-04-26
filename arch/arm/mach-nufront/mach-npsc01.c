/*
 * Copyright (C) 2014 Nufront Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/npsc_apb_timer.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/clocksource.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk-provider.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/mach/map.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <asm/system_misc.h>
#include <asm/hardware/cache-l2x0.h>

#include <mach/core.h>
#include <mach/prcm.h>

#define L2_AUX_VAL 0x7C470001
#define L2_AUX_MASK 0xC200ffff

extern void __init npsc01_map_io(void);

static struct platform_device nufront_cpufreq_pdev = {
   .name = "nusmart-cpufreq",
};


static void __init board_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			NULL, &platform_bus);
	platform_device_register(&nufront_cpufreq_pdev);
}

static void __init nufront_timer_init(void)
{
	of_clk_init(NULL);
	npsc_apb_timer_init();
	clocksource_of_init();
	l2x0_of_init(L2_AUX_VAL, L2_AUX_MASK);
}

static void __init nufront_map_io(void)
{
	debug_ll_io_init();
	npsc01_map_io();
	early_printk("Early printk initialized\n");
}

static int npsc01_set_wake(struct irq_data *data, unsigned int on)
{
	unsigned long reg = 0;
	int bit_idx;

	switch(irqd_to_hwirq(data) - 32) {
	case 16: bit_idx = 1; break;/* wakeup_gpio_intr_flag */
	case 21: bit_idx = 2; break;/* wktimer_irq */
	case 48: bit_idx = 0; break;/* ethernet0_pmt_irq */
	case 59: bit_idx = 6; break;/* uotg_irq */
	case 60: bit_idx = 8; break;/* uhosto0_irq */
	case 61: bit_idx = 8; break;/* uhoste0_irq */
	case 62: bit_idx = 7; break;/* uhosto1_irq */
	case 63: bit_idx = 7; break;/* uhoste1_irq */
	case 65: bit_idx = 9; break;/* usb3_wakup_interrupt */
	case 68: bit_idx = 10; break;/* usb3_wakup_interrupt */
	default: return -ENOENT;
	}

	nufront_prcm_read((u32*)&reg, PRCM_WAKEUP_IRQ_CTRL1);

	if(on)
		set_bit(bit_idx, &reg);
	else
		clear_bit(bit_idx, &reg);

	return nufront_prcm_write((u32)reg, PRCM_WAKEUP_IRQ_CTRL1);
}

static void __init nufront_init_irq(void)
{
	nufront_map_prcm();
	irqchip_init();
	gic_arch_extn.irq_set_wake = npsc01_set_wake;
}

static void nufront_restart(char mode, const char *cmd)
{
	u32 reg;

	nufront_prcm_write(0, PRCM_MISC_REG0);
	nufront_prcm_write(0, PRCM_MISC_REG1);
	nufront_prcm_write(0, PRCM_MISC_REG2);
	nufront_prcm_write(0, PRCM_MISC_REG4);
	nufront_prcm_write(0, PRCM_MISC_REG5);
	nufront_prcm_write(0, PRCM_MISC_REG6);
	nufront_prcm_write(0, PRCM_MISC_REG7);
	nufront_prcm_write(0, PRCM_MISC_REG8);

	nufront_prcm_read(&reg, PRCM_SYS_RST_CTRL);
	nufront_prcm_write(reg & ~BIT(0), PRCM_SYS_RST_CTRL);
	nufront_prcm_write(reg | BIT(0), PRCM_SYS_RST_CTRL);
}

static const char * const npsc01_vm3_board_compat[] = {
	"nufront,npsc01-vm3-board",
	NULL,
};

DT_MACHINE_START(NPSC01_VM3_BOARD, "NPSC01-VM3-BOARD")
	.smp = smp_ops(nufront_smp_ops),
	.map_io = nufront_map_io,
	.init_irq = nufront_init_irq,
	.init_time  = nufront_timer_init,
	.init_machine = board_init,
	.restart = nufront_restart,
	.dt_compat = npsc01_vm3_board_compat,
MACHINE_END
