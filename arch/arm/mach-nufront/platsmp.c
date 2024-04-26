/*
 *
 *  Copyright (C) 2014 Nufront Corporation
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <asm/smp_scu.h>
#include <asm/smp_plat.h>

#include <mach/core.h>

#define NUFRONT_PRCM_DIRECT_ACCESS
#include <mach/prcm.h>



void __init nufront_smp_prepare_cpus(unsigned int max_cpus)
{
	void __iomem *scu_base = NULL;
	unsigned int i;

	scu_base = ioremap(scu_a9_get_base(), SZ_4K);
	if (!scu_base) {
		pr_err("ioremap(scu_base) failed\n");
		return;
	}

	max_cpus = scu_get_core_count(scu_base);
	for (i = 0; i < max_cpus; i++)
		set_cpu_present(i, true);

	scu_enable(scu_base);
	iounmap(scu_base);

}

/*
 * Write pen_release in a way that is guaranteed to be visible to all
 * observers, irrespective of whether they're taking part in coherency
 * or not.  This is necessary for the hotplug code to work reliably.
 */
static void __cpuinit write_pen_release(int val)
{
	pen_release = val;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&pen_release, sizeof(pen_release));
	outer_clean_range(__pa(&pen_release), __pa(&pen_release + 1));
}

static DEFINE_SPINLOCK(boot_lock);

void __cpuinit nufront_secondary_init(unsigned int cpu)
{
	/*
	 * let the primary processor know we're out of the
	 * pen, then head off into the C entry point
	 */
	write_pen_release(-1);

	/*
	 * Synchronise with the boot thread.
	 */

	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);

}

int __cpuinit nufront_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;


	/*
	 * Set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	{
		void __iomem *scu_base = NULL;

		scu_base = ioremap(scu_a9_get_base(), SZ_4K);
		if (!scu_base) {
			pr_err("ioremap(scu_base) failed\n");
			spin_unlock(&boot_lock);
			return -EIO;
		}

		scu_enable(scu_base);
		iounmap(scu_base);
	}


	/*
	 * This is really belt and braces; we hold unintended secondary
	 * CPUs in the holding pen until we're ready for them.  However,
	 * since we haven't sent them a soft interrupt, they shouldn't
	 * be there.
	 */
	write_pen_release(cpu_logical_map(cpu));

	/*
	 * Write the address of secondary startup into the
	 * system-wide flags register. The BootMonitor waits
	 * until it receives a soft interrupt, and then the
	 * secondary CPU branches to this address.
	 */
	__raw_writel(virt_to_phys(nufront_secondary_startup),
		PRCM_MISC_REG0 + (cpu * 8));

	/*
	 * Send the secondary CPU a soft interrupt, thereby causing
	 * the boot monitor to read the system wide flags register,
	 * and branch to the address found there.
	 */
	arch_send_wakeup_ipi_mask(cpumask_of(cpu));

	timeout = jiffies + (1 * HZ);
	while (time_before(jiffies, timeout)) {
		smp_rmb();
		if (pen_release == -1)
			break;

		udelay(10);
	}

	//for testing only
	__raw_writel(0, PRCM_MISC_REG0 + (cpu * 8));


	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return pen_release != -1 ? -ENOSYS : 0;
}

static inline void cpu_enter_lowpower(void)
{
}

static inline void cpu_leave_lowpower(void)
{
}

static inline void platform_do_lowpower(unsigned int cpu)
{
	/* Just enter wfi for now. TODO: Properly shut off the cpu. */
	for (;;) {
		/*
		 * here's the WFI
		 */
		asm("wfi"
				:
				:
				: "memory", "cc");

		if (pen_release == cpu_logical_map(cpu)) {
			/*
			 * OK, proper wakeup, we're done
			 */
			break;
		}

		/*
		 * getting here, means that we have come out of WFI without
		 * having been woken up - this shouldn't happen
		 *
		 * The trouble is, letting people know about this is not really
		 * possible, since we are currently running incoherently, and
		 * therefore cannot safely call printk() or anything else
		 */
		pr_debug("CPU%u: spurious wakeup call\n", cpu);
	}
}

/*
 * platform-specific code to shutdown a CPU
 *
 * Called with IRQs disabled
 */
void __ref nufront_cpu_die(unsigned int cpu)
{
	/*
	 * we're ready for shutdown now, so do it
	 */
	cpu_enter_lowpower();
	platform_do_lowpower(cpu);

	/*
	 * bring this CPU back into the world of cache
	 * coherency, and then restore interrupts
	 */
	cpu_leave_lowpower();
}

struct smp_operations nufront_smp_ops __initdata = {
	.smp_prepare_cpus   = nufront_smp_prepare_cpus,
	.smp_boot_secondary = nufront_boot_secondary,
	.smp_secondary_init = nufront_secondary_init,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die        = nufront_cpu_die,
#endif
};
