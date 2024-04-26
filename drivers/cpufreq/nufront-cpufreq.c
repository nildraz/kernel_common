/*
 * Copyright (C) 2011 NUFRONT, Inc.
 *
 * Author:
 *	zeyuan <zeyuan.xu@nufront.com>
 *	Based on Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*#define DEBUG*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/suspend.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <asm/system.h>
#include <linux/regulator/consumer.h>

#define CLK_NAME		"cpu_clk"
#define REGU_NAME		"vdd_cpu"
#define DEFAULT_VOL_FREQ	1200000
#define NUM_CPUS	4

#if 1
#define cpufreq_debug(fmt, args...) pr_info("cpufreq_debug: " fmt "\n", ##args)
#else
#define cpufreq_debug(...)
#endif

#define CPU_INITIAL_VOL 1050

static unsigned long target_cpu_speed[NUM_CPUS];
static struct cpufreq_frequency_table freq_table[] = {
	{ 0, 200000 },
	{ 1, 600000 },
	{ 2, 1000000 },
#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE
	{ 3, 50000 },
	{ 4, 100000 },
	{ 5, 250000 },
	{ 6, 300000 },
	{ 7, 350000 },
	{ 8, 400000 },
	{ 9, 450000 },
	{ 10, 500000 },
	{ 11, 550000 },
	{ 12, 650000 },
	{ 13, 700000 },
	{ 14, 750000 },
	{ 15, 800000 },
	{ 16, 850000 },
	{ 17, 900000 },
	{ 18, 950000 },
	{ 19, 1000000 },
	{ 20, 1100000 },
	{ 21, 1150000 },
	{ 22, 1250000 },
	{ 23, 1300000 },
	{ 24, 1350000 },
	{ 25, 1400000 },
	{ 26, 1450000 },
	{ 27, 1500000 },
	{ 28, 1600000 },
#endif
	{ 29, CPUFREQ_TABLE_END },
};

static struct clk *cpu_clk;
static struct regulator	*cpu_regu;
static struct device *cpu_dev;

static DEFINE_MUTEX(nusmart_cpu_lock);
static bool is_suspended;
static int cpu_vol;

#define RAMP_DOWN_TIMES (2)
static int ramp_down_count;

struct fvs {
	unsigned long	rate;	/*KHz*/
	int		vol;	/*mV*/
};

const static struct fvs fvs_table[] = {
	{ 200000, 1300},
	{ 600000, 1300},
	{ 1000000, 1300},
#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE
	{ 50000, 1000 },
	{ 100000, 1000 },
	{ 250000, 1000 },
	{ 300000, 1000 },
	{ 350000, 1000 },
	{ 400000, 1000 },
	{ 450000, 1000 },
	{ 500000, 1000 },
	{ 550000, 1000 },
	{ 650000, 1000 },
	{ 700000, 1000},
	{ 750000, 1000 },
	{ 800000, 1000 },
	{ 850000, 1000 },
	{ 900000, 1000 },
	{ 950000, 1000 },
	{ 1000000, 1000 },
	{ 1100000, 1000 },
	{ 1150000, 1000 },
	{ 1250000, 1000 },
	{ 1300000, 1000 },
	{ 1350000, 1000 },
	{ 1400000, 1000 },
	{ 1450000, 1000 },
	{ 1500000, 1000 },
	{ 1600000, 1000 },
	{ 1700000, 1000 },
	{ 1800000, 1000 },
#endif
};

int nusmart_verify_speed(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

unsigned int nusmart_getspeed(unsigned int cpu)
{
	unsigned long rate;

	if (cpu >= NUM_CPUS)
		return 0;

	rate = clk_get_rate(cpu_clk) / 1000;
	return rate;
}

inline int get_target_voltage(unsigned long rate)
{
	int idx = 0;
	for (idx = 0; idx < ARRAY_SIZE(fvs_table); idx++) {
		if (fvs_table[idx].rate == rate)
			return fvs_table[idx].vol;
	}
	WARN_ON(1);
	return fvs_table[ARRAY_SIZE(fvs_table)-1].vol;
}

static int nusmart_cpu_clk_set_rate(unsigned long rate)
{
	int ret, i;
	struct clk *parent;

	for (i = 0; i < ARRAY_SIZE(freq_table); i++) {
		if (freq_table[i].frequency == CPUFREQ_TABLE_END) {
			pr_err("Not support this rate!\n");
			return -EINVAL;
		}

		if (freq_table[i].frequency * 1000 != rate)
			continue;
		else
			break;
	}
    clk_prepare(cpu_clk);
	{
		ret = clk_get_rate(cpu_clk);
		printk("cpufre:now cpu clk is %d\n", ret);
		if (ret != rate) {
			printk("cpufre:set cpu clk \n");
			ret = clk_set_rate(cpu_clk, rate);
			if (ret) {
				pr_err("Failed to change cpu clk to %lu\n", rate);
				goto out;
			}
		}
		ret = clk_get_rate(cpu_clk);
		printk("cpufre:after set %u  cpu clk ,clk=%d\n",rate, ret);
	}

	return 0;

out:
	return ret;
}

static int nusmart_update_cpu_speed(struct cpufreq_policy *policy, unsigned long rate)
{
	int ret = 0;
	int cpu_vol_new = get_target_voltage(rate);
	struct cpufreq_freqs freqs;

	freqs.old = nusmart_getspeed(0);
	freqs.new = rate;

	if (freqs.old == freqs.new)
		return ret;
	else if (freqs.new > freqs.old)
		ramp_down_count = 0;
	else if (++ramp_down_count < RAMP_DOWN_TIMES)
		return ret;

	/*
	 * Vote on memory bus frequency based on cpu frequency
	 * This sets the minimum frequency, display or avp may request higher
	 */

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
#ifdef DEBUG
	cpufreq_debug("cpufreq-nusmart: transition: %u --> %u",
			freqs.old, freqs.new);
#endif
	if ((cpu_vol_new == cpu_vol) || (NULL == cpu_regu)) {
		/*
		 * only update clock when there is no regulator
		 * or there is no need to update cpu voltage.
		 */
		ret = nusmart_cpu_clk_set_rate(freqs.new * 1000);
		if (ret) {
			pr_err("cpu-nusmart: Failed to set cpu frequency to %d kHz\n",
					freqs.new);
			return ret;
		}
	} else if (cpu_vol_new > cpu_vol) {
		/*
		 * rise up voltage before scale up cpu frequency.
		 */
		//ret = regulator_set_voltage(cpu_regu, cpu_vol_new * 1000, cpu_vol_new * 1000);
		//if (ret < 0) {
		//	pr_err("cpu-nusmart: Failed to set cpu voltage to %d mV\n", cpu_vol_new);
		//	return ret;
		//}

		ret = nusmart_cpu_clk_set_rate(freqs.new * 1000);
		if (ret) {
			pr_err("cpu-nusmart: Failed to set cpu frequency to %d kHz\n",
					freqs.new);
			return ret;
		}

	} else {
		/*
		 * reduce voltage after scale down cpu frequency.
		 */
		ret = nusmart_cpu_clk_set_rate(freqs.new * 1000);
		if (ret) {
			pr_err("cpu-nusmart: Failed to set cpu frequency to %d kHz\n",
					freqs.new);
			return ret;
		}

		//ret = regulator_set_voltage(cpu_regu, cpu_vol_new * 1000, cpu_vol_new * 1000);
		//if (ret < 0)
		//	pr_err("cpu-nusmart: Failed to set cpu voltage to %d mV\n", cpu_vol_new);
	}

	cpu_vol = cpu_vol_new;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned long nusmart_cpu_highest_speed(void)
{
	unsigned long rate = 0;

	/*avold target rate exceed default max rate with default voltage*/
	if (cpu_regu == NULL)
		rate = min(rate, (unsigned long)DEFAULT_VOL_FREQ);
	return rate;
}

static int nusmart_target(struct cpufreq_policy *policy,
		unsigned int target_freq,
		unsigned int relation)
{
	int idx;
	unsigned int freq;
	int ret = 0;
	int rate = 0;

	mutex_lock(&nusmart_cpu_lock);

	ret = cpufreq_frequency_table_target(policy, freq_table, target_freq,
			relation, &idx);
	if (ret) {
		pr_err("failed to match target freqency %d: ret=%d, idx=%d\n",
		       target_freq, ret, idx);
		return ret;
	}

	freq = freq_table[idx].frequency;

	rate = nusmart_cpu_highest_speed();

	if (rate == 0)
		rate = freq;
	ret = nusmart_update_cpu_speed(policy, rate);

	mutex_unlock(&nusmart_cpu_lock);
	return ret;
}

static int nusmart_pm_notify(struct notifier_block *nb, unsigned long event,
		void *dummy)
{
	mutex_lock(&nusmart_cpu_lock);
	if (event == PM_SUSPEND_PREPARE) {
		is_suspended = true;
		pr_info("nusmart cpufreq suspend: setting frequency to %d kHz\n",
				DEFAULT_VOL_FREQ);
	} else if (event == PM_POST_SUSPEND) {
		is_suspended = false;
	}
	mutex_unlock(&nusmart_cpu_lock);

	return NOTIFY_OK;
}

static struct notifier_block nusmart_cpu_pm_notifier = {
	.notifier_call = nusmart_pm_notify,
};

static int nusmart_cpu_init(struct cpufreq_policy *policy)
{

	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = nusmart_getspeed(policy->cpu);
	target_cpu_speed[policy->cpu] = policy->cur;

	/* FIXME: what's the actual transition time? */
	policy->cpuinfo.transition_latency = 300 * 1000;

	cpumask_copy(policy->cpus, cpu_possible_mask);

	if (policy->cpu == 0)
		register_pm_notifier(&nusmart_cpu_pm_notifier);

	return 0;
}

static int nusmart_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	clk_put(cpu_clk);
	return 0;
}

static struct freq_attr *nusmart_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver nusmart_cpufreq_driver = {
	.verify		= nusmart_verify_speed,
	.target		= nusmart_target,
	.get		= nusmart_getspeed,
	.init		= nusmart_cpu_init,
	.exit		= nusmart_cpu_exit,
	.name		= "nusmart",
	.attr		= nusmart_cpufreq_attr,
};

static int __init nusmart_cpu_regu_init(void)
{
	/*initialize cpu regulator after regulator ready*/
	cpu_regu = regulator_get(NULL, REGU_NAME);
	if (!IS_ERR(cpu_regu)) {
		cpu_vol = regulator_get_voltage(cpu_regu) / 1000;
		pr_info("cpu freq:cpu voltage:%dmV\n", cpu_vol);
		if (cpu_vol != CPU_INITIAL_VOL) {
			regulator_set_voltage(cpu_regu, CPU_INITIAL_VOL * 1000, CPU_INITIAL_VOL * 1000);
			pr_info("cpu freq: set cpu voltage:%dmV\n", CPU_INITIAL_VOL);
		}
	} else {
		cpu_regu = NULL;
		pr_err("cpu freq:get regulator failed.\n");
	}
	return 0;
}

static int nusmart_cpufreq_probe(struct platform_device *pdev)
{
	struct device_node *np;

	pr_info(" probe cpu freq -\n");
	cpu_dev = &pdev->dev;
    printk("Jules add nusmart_cpufreq_probe\n");
	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		dev_err(cpu_dev, "failed to find cpu0 node\n");
		return -ENOENT;
	}

	cpu_dev->of_node = np;
	//nusmart_cpu_regu_init();

	cpu_clk = devm_clk_get(cpu_dev, "cpu_clk");
	if (IS_ERR(cpu_clk)) {
		pr_err("cpu freq : get clock failed.\n");
		return PTR_ERR(cpu_clk);
	}
	return cpufreq_register_driver(&nusmart_cpufreq_driver);
}

static struct platform_driver nusmart_cpufreq_platdrv = {
	.driver = {
		.name = "nusmart-cpufreq",
		.owner = THIS_MODULE,
	},
	.probe = nusmart_cpufreq_probe,
};

module_platform_driver(nusmart_cpufreq_platdrv);

MODULE_AUTHOR("zeyuan <zeyuan.xu@nufront.com>");
MODULE_DESCRIPTION("cpufreq driver for nusmart");
MODULE_LICENSE("GPL");
