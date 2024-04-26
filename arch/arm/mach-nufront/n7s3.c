#include <linux/io.h>
#include <linux/module.h>
#include <linux/kbuild.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/wakelock.h>
#include <linux/clk.h>
#include <linux/input.h>
#include <linux/kmsg_dump.h>
#include <linux/cpu.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/firmware.h>
#include <linux/suspend.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/delay.h>
#include <linux/ctype.h>


//#include <linux/regulator/machine.h>

//#include <asm/sched_clock.h>
#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/suspend.h>
#include <asm/idmap.h>
#include <asm/mach/map.h>
#include <mach/n7s3.h>
#define NUFRONT_PRCM_DIRECT_ACCESS
#include <mach/prcm.h>
#include <mach/prcm-npsc01.h>
#include <mach/core.h>
#include <mach/hardware.h>
//#include <mach/pinctrl-nufront.h>
#include <linux/random.h>

extern void __iomem *get_prcm_base(void);
extern int nufront_prcm_write(u32 val, u32 offset);
extern int nufront_prcm_read(u32 *val, u32 offset);
void WakeupTimerSetClk32K(int num);
void WakeupTimerClrClk32K(int num);


//#define TEST

#define S3_DRIVER_NAME		"s3"
#define MAX_LEN				9
#define MIN_LEN				4

#define TIMER_LCNT			0x0
#define TIMER_CVAL			0x4
#define TIMER_CTRL			0x8
#define TIMER_EOI			0xC
#define TIMER_INTS			0x10
#define TIMER_OFFSET		0x14

#define TIMER_IRQ_OFFSET	(32 + 21)
#define TIMER_CLK_KHZ		24000
#define _TIMER_ENABLE		(1<<0)
#define _TIMER_USER_DEFINE	(1<<1)
#define _TIMER_INTEN		(1<<2)

#define DCTRL_DEBUG_REG		0x050e00c0

#define T_RANDOM_TEST		0x1
#define T_EARLY_SUSPEND		0x2
#define T_ESHRTIMER			0x4
#define T_PMN_TEST			0x8
#define T_SCF_TEST			0x10
#define T_UPDATE_SC			0x20	/* modification */
#define T_WAKELOCK_C		0x40
#define T_SW_TIMER			0x80
#define T_CPUIDLE			0x100

#define T_USE_TASKLET		0x40000000
#define T_USE_WORKQUEUE		0x80000000
#define TIMER_BASE 0x05220000

/*
 * T_USE_WORKQUEUE | T_SCF_TEST | T_SW_TIMER | T_WAKELOCK_C | T_UPDATE_SC
 * T_USE_TASKLET | T_SCF_TEST | T_SW_TIMER | T_WAKELOCK_C | T_UPDATE_SC
 */
unsigned int dtf = T_USE_WORKQUEUE | T_SCF_TEST | T_SW_TIMER;
#if 0
unsigned int dtf = 0;
unsigned int dtf = T_USE_TASKLET | T_SCF_TEST | T_SW_TIMER;
#endif

static unsigned int suspend_done = 0;
static unsigned long suspend_fail = 0;

/* early_suspend test */
struct input_dev *est_dev = NULL;
#if defined(CONFIG_PM_AUTO_WAKEUP_TEST)
int wktimer_auto_wakeup = 1;
#else
int wktimer_auto_wakeup = 0;
#endif

#ifdef CONFIG_PM_AUTO_TEST_SUSPEND_619_RTC
int rtc_auto_wakeup = 1;
#else
int rtc_auto_wakeup = 0;
#endif
int auto_wakeup = 0;

static void __iomem *sarram;
static void __iomem *timer1_base;
static void __iomem *timer_base;

#define MAX_BUF_SIZE	1024
static int random_pattern = 0x6b;
module_param_named(random_pattern, random_pattern, int, S_IRUGO | S_IWUSR | S_IWGRP);

static struct debug_timer *debug_timer = NULL;

static int wqctrl = 0;
module_param_named(wqctrl, wqctrl, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define CFM_SELF_TEST		0x1
/*
 * Send SGI to an cpu, don't care if it's online or offline(shutdown),
 * Of course u can't rock a cpu which is shutdown.
 */
#define CFM_OFFLINE_TEST	0x2
static int call_func_mode = 0;
module_param_named(call_func_mode, call_func_mode, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int cfs_wait = 0;
module_param_named(cfs_wait , cfs_wait, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int cfs_buf_sz = 64;
module_param_named(cfs_buf_sz, cfs_buf_sz, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int mem_leaker = 0;
module_param_named(mem_leaker, mem_leaker, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int apply_affinity = 0;
module_param_named(apply_affinity, apply_affinity, int, S_IRUGO | S_IWUSR | S_IWGRP);

static int screen_test_cnt = 0;
static int screen_test = 0;
module_param_named(screen_test, screen_test, int, S_IRUGO | S_IWUSR | S_IWGRP);

static unsigned int test_id = 6;
static unsigned int test_ms = 1000;
module_param_named(test_id, test_id, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(test_ms, test_ms, int, S_IRUGO | S_IWUSR | S_IWGRP);

/*
 * [3:0] used for paths
 * [7:4] used for ramv
 * [8] used for auto_wakeup
 * [9] MODE_POLL_ASR
 * [10] MODE_DDR_ASR
 * [11] MODE_CLK_WORKAROUND
 * [31:12] sleep time in ms for auto_wakeup
 * XXX auto_wakeup may not work in case when clkstop/flight mode is set
 */
static unsigned long suspend_ctrl = 0xbb8844;
module_param_named(suspend_ctrl, suspend_ctrl, ulong, S_IRUGO | S_IWUSR | S_IWGRP);

static unsigned int resume_path = 0;
module_param_named(resume_path, resume_path, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define MAX_RETRY		(2)
#define EHR_STATE_RESUME 	(0)
#define EHR_STATE_SUSPEND	(3)
/* Make sure this value is enough for early_suspend/late_resume to complete */
static unsigned int ehr_interval = 5/* seconds */;
module_param_named(ehr_interval, ehr_interval, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define MAX_REGS_LEN		200
#define DUMP_REGS		(0x1<<0)
#define DUMP_DDR_REGS		(0x1<<1)
#define DUMP_CONSTRUCT		(0x1<<2)
#define DUMP_ESHRTIMER		(0x1<<3)
static unsigned int dump = 1;
module_param_named(dump, dump, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define FLOW_PREPARE		(0x1<<0)
#define FLOW_SUSPEND		(0x1<<1)
#define FLOW_SUSPEND_LATE	(0x1<<2)
#define FLOW_SUSPEND_NOIRQ	(0x1<<3)
#define FLOW_RESUME_NOIRQ	(0x1<<4)
#define FLOW_RESUME_EARLY	(0x1<<5)
#define FLOW_RESUME		(0x1<<6)
#define FLOW_RESUME_COMPLETE	(0x1<<7)
static unsigned int flow_buggy = 0;
module_param_named(flow_buggy, flow_buggy, int, S_IRUGO | S_IWUSR | S_IWGRP);



/*
 * NPSC01 IO Table
 *
 */
struct map_desc npsc01_io_desc[] __initdata = {
   {
		.virtual = IO_ADDRESS(NPSC01_DRAM_RESERVED_BASE),
		.pfn	 = __phys_to_pfn(NPSC01_DRAM_RESERVED_BASE),
		.length  = SZ_4K + SZ_8K,
	    .type	 =  MT_DEVICE
   },
   {
		.virtual = IO_ADDRESS(NPSC01_IRAM_BASE),
		.pfn	 = __phys_to_pfn(NPSC01_IRAM_BASE),
		.length	 = SZ_64K,
		.type	 = MT_DEVICE
   },
   {
		.virtual = IO_ADDRESS(SARRAM_BASE),
		.pfn	 = __phys_to_pfn(SARRAM_BASE),
		.length  = SZ_8K,
		.type	 = MT_DEVICE
   }
};

void __init npsc01_map_io(void)
{
	iotable_init(npsc01_io_desc, ARRAY_SIZE(npsc01_io_desc));
}


static void dump_priv_regs(void __iomem *start, unsigned int len, char *stage, int force_dump)
{
	unsigned int i;

	len = 4 * ((len + 3) / 4);

	if(len > MAX_REGS_LEN)
		len = MAX_REGS_LEN;

	if((dump & DUMP_REGS) || force_dump) {
		printk(KERN_DEBUG "%s\n", stage);
		for(i = 0; i < len; i += 4) {
			printk(KERN_DEBUG "[%08x] %08x %08x %08x %08x\n",
					(unsigned int)(start + 4 * i),
					readl(start + 4 * i),
					readl(start + 4 * (i + 1)),
					readl(start + 4 * (i + 2)),
					readl(start + 4 * (i + 3)));
		}
	}
}

#define ARM_CA9		0xC09
#define ARM_CA7		0xC07
static unsigned long g_cpu_ppn;
static unsigned long get_cpu_ppn(void)
{
	printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
	asm("mrc p15, 0, %0, c0, c0, 0" : "=r"(g_cpu_ppn) : : "cc");
	printk("%s:%s:%d\n", __FILE__, __func__, __LINE__);
	g_cpu_ppn = (g_cpu_ppn >> 4) & 0xFFF;
	return g_cpu_ppn;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static DEFINE_MUTEX(priv_resume_lock);
static LIST_HEAD(priv_resume_handlers);

void register_priv_resume(struct priv_resume *handler)
{
	struct list_head *pos;

	mutex_lock(&priv_resume_lock);
	list_for_each(pos, &priv_resume_handlers) {
		struct priv_resume *pr;
		pr = list_entry(pos, struct priv_resume, link);

		if(pr->level > handler->level)
			break;
	}
	list_add_tail(&handler->link, pos);
	mutex_unlock(&priv_resume_lock);
}
EXPORT_SYMBOL(register_priv_resume);

void unregister_priv_resume(struct priv_resume *handler)
{
	mutex_lock(&priv_resume_lock);
	list_del(&handler->link);
	mutex_unlock(&priv_resume_lock);
}
EXPORT_SYMBOL(unregister_priv_resume);

void priv_resume(void)
{
	struct priv_resume *pos;

	mutex_lock(&priv_resume_lock);
	list_for_each_entry(pos, &priv_resume_handlers, link) {
		if(pos->resume) {
			printk(KERN_DEBUG "Call %pf\n", pos->resume);
			pos->resume(pos);
		}
	}
	mutex_unlock(&priv_resume_lock);
}
EXPORT_SYMBOL(priv_resume);
#endif


#ifdef TEST
#define NR_DDR_CFG_ID		(2)
#define CFG_ID_MASK		(0x3)

/* max number of regs should be config'ed */
#define NR_DCTRL_REGS		(72)
#define NR_DPHY_REGS		(41)

/* total number of ddr3_freqxxx_vars or lpddr2_freqxxx_vars */
#define NR_DDR_VARS		(7)

#define DDR_PHY_OFFSET		(0x400)
#define DDR_MAX_OFFSET		(0x578)
#define DDR_INVALID_OFFSET	(0xFFF)

/* used to save ddr_sel settings */
#define DDR_PHASE_1_OFFSET	(0x400)
/* used to save ddr_vars */
#define DDR_PHASE_2_OFFSET	(0x3D0)

/* 60K */
#define IRAM_RESERVED_OFFSET	(0xF000)

/* check cfg_id 0 dram_type */
int is_support_lpddr2(void)
{
	void __iomem *addr = __io_address(NPSC01_DRAM_RESERVED_BASE);
	printk("%s:%s:%d addr 0x%08x\n", __FILE__, __func__, __LINE__, addr);
	return (readl(addr) >> 28) ? 1 : 0;
}

/*********************** lpddr2 *********************/
static unsigned int lpddr2_freq400_vars[] = {
	0x141a1060,	0x36db36db,	0x36db36db,
	0x36db36db,	0x40004000,	0x0,
	0x40004000
};

/* same as freq400? need to verify */
static unsigned int lpddr2_freq333_vars[] = {
	0x141a1060,	0x36db36db,	0x36db36db,
	0x36db36db,	0x40004000,	0x0,
	0x40004000
};

/*********************** ddr3 *********************/
static unsigned int ddr3_freq400_vars[] = {
	0x02221060,	0xf69af69a,	0xf75df924,
	0xf69af69a,	0x7c71769a,	0x0,
	0x40004000
};

/* same as freq400 ? */
static unsigned int ddr3_freq333_vars[] = {
	0x02221060,	0xf69af69a,	0xf75df924,
	0xf69af69a,	0x7c71769a,	0x0,
	0x40004000
};

#if defined(CONFIG_MACH_TL7689_PAD_TEST) || defined(CONFIG_MACH_TL7689_PAD_AURORA)
struct ddr_config target_ddr_cfg[] = {
	/* ddr3 h5tq4g63afr_pbc_freq333_cl5_bl8 */
	{0x0, 0x00000600},
	{0x8, 0x00000004},
	{0x14, 0x01000000},
	{0x18, 0x0001046b},
	{0x1c, 0x00028b0b},
	{0x20, 0x0400050a},
	{0x24, 0x0c110404},
	{0x28, 0x040e0504},
	{0x2c, 0x00000c04},
	{0x30, 0x03005b68},
	{0x34, 0x05050003},
	{0x38, 0x01010000},
	{0x3c, 0x0005030a},
	{0x40, 0x01000000},
	{0x44, 0x0a200057},
	{0x48, 0x00000300},
	{0x4c, 0x0200000a},
	{0x50, 0x0000005a},
	{0x54, 0x05000001},
	{0x58, 0x00000005},
	{0x74, 0x00460210},
	{0x78, 0x00000400},
	{0x80, 0x00021000},
	{0x84, 0x00000046},
	{0x90, 0x01010000},
	{0xa0, 0x01000200},
	{0xa4, 0x02000040},
	{0xb0, 0x01010001},
	{0xb4, 0x01ffff0a},
	{0xb8, 0x01010101},
	{0xbc, 0x01010101},
	{0xc0, 0x00000103},
	{0xc4, 0x00000c03},
	//{0xc8, 0x00000100},
	{0xcc, 0x00000001},
	{0x104, 0x02010102},
	{0x108, 0x01060604},
	{0x10c, 0x02020001},
	{0x110, 0x01020201},
	{0x114, 0x00000200},
	{0x124, 0x281a0000},
	//{0x13c, 0x00000100},
	{0x148, 0x00402000},
	{0x158, 0x00004020},
	{0x164, 0x00402000},
	{0x174, 0x00004020},
	{0x17c, 0x00013203},
	{0x180, 0x32000132},
	{0x184, 0x01320001},
	{0x190, 0x00000700},
	{0x194, 0x00144000},
	{0x198, 0x02000200},
	{0x19c, 0x02000200},
	{0x1a0, 0x00001440},
	{0x1a4, 0x00006540},
	{0x1a8, 0x01020505},
	{0x1ac, 0x00140303},
	{0x1b8, 0x04038000},
	{0x1bc, 0x07030307},
	{0x1c0, 0x00ffff16},
	{0x1c4, 0x00190010},
	{0x1dc, 0x00000204},
	{0x1e8, 0x00000001},
	{0x200, 0x00000040},
	{0x204, 0x02020000},
	{0x208, 0x00020200},
	{0x20c, 0x02000202},
	{0x210, 0x00000002},
	/* phy */
	{0x400,	0x26122612},
	{0x404, 0x26142614},
	{0x408, 0x00d90069},
	{0x40c, 0x00721e1a},
	{0x410, 0x20202020},
	{0x440, 0x26122612},
	{0x444, 0x26142614},
	{0x448, 0x00d90069},
	{0x44c, 0x00721e1a},
	{0x450, 0x20202020},
	{0x480, 0x26122612},
	{0x484, 0x26142614},
	{0x488, 0x00d90069},
	{0x48c, 0x00721e1a},
	{0x490, 0x20202020},
	{0x4c0, 0x26122612},
	{0x4c4, 0x26142614},
	{0x4c8, 0x00d90069},
	{0x4cc, 0x00721e1a},
	{0x4d0, 0x20202020},
	{0x500, 0x26122612},
	{0x504, 0x26142614},
	{0x508, 0x00d90069},
	{0x50c, 0x00721e1a},
	{0x510, 0x38383838},
	{0x544, 0x00005004},
	{0x548, 0x40004000},
	{0x54c, 0x7fff7fff},
	{0x550, 0x7fff7fff},
	{0x554, 0x7fff7fff},
	{0x558, 0x40004000},
	{0x55c, 0x40004000},
	{0x560, 0x40004000},
	{0x564, 0x7fff7fff},
	{0x568, 0x7fff7fff},
	{0x56c, 0x7fff7fff},
	{0x570, 0x40004000},
	{0x574, 0x40004000},
	{0x578, 0x40004000},
	/* mark end */
	{DDR_INVALID_OFFSET,},
};
#endif

//#if defined(CONFIG_MACH_TL7689_PHONE_TEST) || defined(CONFIG_MACH_TL7689_PHONE_ROC)
struct ddr_config target_ddr_cfg[] = {
	/* jedec_lpddr2s4_4gb_x32_1066_freq333_cl5_bl8 */
	{0x0, 0x00000500},
	{0x8, 0x00000022},
	{0xc, 0x0001046b},
	{0x10, 0x0000014e},
	{0x14, 0x01000d06},
	{0x18, 0x00000043},
	{0x1c, 0x000000a7},
	{0x20, 0x0200020a},
	{0x24, 0x0e150402},
	{0x28, 0x03110603},
	{0x2c, 0x00000505},
	{0x30, 0x03005b25},
	{0x34, 0x05060005},
	{0x38, 0x01010002},
	{0x3c, 0x0007030b},
	{0x40, 0x01000000},
	{0x44, 0x050c002c},
	{0x48, 0x00000300},
	{0x4c, 0x002f000a},
	{0x50, 0x0000002f},
	{0x54, 0x02000001},
	{0x58, 0x00000002},
	{0x70, 0x02000000},
	{0x74, 0x00630000},
	{0x78, 0x00000003},
	{0x7c, 0x00000001},
	{0x84, 0x00030063},
	{0x88, 0x00010000},
	{0x90, 0x01010000},
	{0xa0, 0x0078014e},
	{0xa4, 0x0200001e},
	{0xac, 0x00001100},
	{0xb0, 0x01020001},
	{0xb4, 0x01ffff0a},
	{0xb8, 0x01010101},
	{0xbc, 0x01010101},
	{0xc0, 0x00000103},
	{0xc4, 0x01000c03},
	{0xcc, 0x00000001},
	{0x104, 0x02020101},
	{0x110, 0x01020201},
	{0x114, 0x00000200},
	{0x118, 0x00000102},
	{0x124, 0x281a0000},
	{0x148, 0x00404000},
	{0x158, 0x00004040},
	{0x164, 0x00404000},
	{0x174, 0x00004040},
	{0x17c, 0x00013203},
	{0x180, 0x32000132},
	{0x184, 0x01320001},
	{0x190, 0x00000a00},
	{0x194, 0x000a1800},
	{0x198, 0x02000200},
	{0x19c, 0x02000200},
	{0x1a0, 0x00000a18},
	{0x1a4, 0x00003278},
	{0x1a8, 0x01020205},
	{0x1ac, 0x00140303},
	{0x1b8, 0x04038000},
	{0x1bc, 0x07030307},
	{0x1c0, 0x00ffff19},
	{0x1c4, 0x00190010},
	{0x1dc, 0x00000204},
	{0x1e8, 0x00000001},
	{0x200, 0x00000040},
	{0x204, 0x02020000},
	{0x208, 0x00020200},
	{0x20c, 0x02000202},
	{0x210, 0x00030002},
	{0x400, 0x26122612},
	{0x404, 0x26142614},
	{0x408, 0x61210071},
	{0x40c, 0x01121e25},
	{0x410, 0x20202020},
	{0x440, 0x26122612},
	{0x444, 0x26142614},
	{0x448, 0x61210071},
	{0x44c, 0x01121e25},
	{0x450, 0x20202020},
	{0x480, 0x26122612},
	{0x484, 0x26142614},
	{0x488, 0x61210071},
	{0x48c, 0x01121e25},
	{0x490, 0x20202020},
	{0x4c0, 0x26122612},
	{0x4c4, 0x26142614},
	{0x4c8, 0x61210071},
	{0x4cc, 0x01121e25},
	{0x4d0, 0x20202020},
	{0x500, 0x26122612},
	{0x504, 0x26142614},
	{0x508, 0x61210071},
	{0x50c, 0x01121e25},
	{0x510, 0x20202020},
	{0x544, 0x00006805},
	{0x548, 0x40004000},
	{0x54c, 0x7fff7fff},
	{0x550, 0x7fff7fff},
	{0x554, 0x7fff7fff},
	{0x558, 0x40004000},
	{0x55c, 0x40004000},
	{0x560, 0x40004000},
	{0x564, 0x7fff7fff},
	{0x568, 0x7fff7fff},
	{0x56c, 0x7fff7fff},
	{0x570, 0x40004000},
	{0x574, 0x40004000},
	{0x578, 0x40004000},
	{DDR_INVALID_OFFSET,},
};
//#endif

/*
 * construct a new config, and save it to preserved ddr region
 * cfg_id 0 is stored by bootloader, should NOT be overwritten
 */
void construct_ddr_cfg(unsigned int cfg_id)
{
	int i, max;
	unsigned int offset, val, phy_addr;
	unsigned int *ddr_vars;
	struct ddr_config *t = NULL;
	void __iomem *addr = __io_address(NPSC01_DRAM_RESERVED_BASE +
					DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK));

	printk("%s:%s:%d addr 0x%08x\n", __FILE__, __func__, __LINE__, addr);

	if(is_support_lpddr2()) {
		ddr_vars = lpddr2_freq333_vars;
	} else {
		ddr_vars = ddr3_freq333_vars;
	}

	t = target_ddr_cfg;
	phy_addr = NPSC01_DRAM_RESERVED_BASE + DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK);
	max = NR_DCTRL_REGS + NR_DPHY_REGS;
	/* construct ddr reg settings */
	for(i = 0; i < max; i++) {
		offset = t[i].offset;
		if(offset == DDR_INVALID_OFFSET)
			break;

		val = t[i].value;
		writel(offset, addr + 8 * i);
		writel(val, addr + 8 * i + 4);
		if(dump & DUMP_CONSTRUCT)
			printk(KERN_DEBUG "[%08x] off=%03x val=%08x\n",
					phy_addr + 8 * i, offset, val);
	}

	/* construct ddr_vars */
	for(i = 0; i < NR_DDR_VARS; i++) {
		writel(ddr_vars[i], addr + DDR_PHASE_2_OFFSET + i * 4);
	}

	iounmap(addr);
}

/* save ddr_sel settings to iram */
static void ddr_save_phase1(unsigned char cfg_id)
{
	unsigned int saved_val, offset;
	int i, max = NR_DCTRL_REGS + NR_DPHY_REGS;
	unsigned int iram_saved_paddr =
		NPSC01_IRAM_BASE + IRAM_RESERVED_OFFSET
			+ DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK);
	void __iomem *saved_addr = __io_address(NPSC01_DRAM_RESERVED_BASE) +
		DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK);

	void __iomem *stored_addr = __io_address(NPSC01_IRAM_BASE + IRAM_RESERVED_OFFSET
			+ DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK));

	for(i = 0; i < max; i++) {
		offset = readl(saved_addr + i * 8);
		if(offset >= DDR_PHY_OFFSET)
			break;
		saved_val = readl(saved_addr + i * 8 + 4);

		writel(offset, stored_addr + i * 8);
		writel(saved_val, stored_addr + i * 8 + 4);
		if(dump & DUMP_DDR_REGS)
			printk(KERN_DEBUG "[%08x] off=%03x val=%08x\n",
					iram_saved_paddr + i * 8, offset, saved_val);
	}

	for(; i < max; i++) {
		offset = readl(saved_addr + i * 8);
		saved_val = readl(saved_addr + i * 8 + 4);

		writel(offset, stored_addr + i * 8);
		writel(saved_val, stored_addr + i * 8 + 4);
		if(dump & DUMP_DDR_REGS)
			printk(KERN_DEBUG "[%08x] off=%03x val=%08x\n",
					iram_saved_paddr + i * 8, offset, saved_val);

		if(offset >= DDR_MAX_OFFSET)
			break;
	}
}

/* Copy ddr_vars[] from ddr to iram */
static void ddr_save_phase2(unsigned char cfg_id)
{
	int i;
	void __iomem *base_addr = __io_address(NPSC01_DRAM_RESERVED_BASE) +
		DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK) + DDR_PHASE_2_OFFSET;

	void __iomem *iram_ddr_vars_base = __io_address(NPSC01_IRAM_BASE) +
		IRAM_RESERVED_OFFSET +
		DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK) + DDR_PHASE_2_OFFSET;
	unsigned int phy_addr = NPSC01_IRAM_BASE + IRAM_RESERVED_OFFSET
		 + DDR_PHASE_1_OFFSET * (cfg_id & CFG_ID_MASK) + DDR_PHASE_2_OFFSET;

	for(i = 0; i < NR_DDR_VARS; i++) {
		writel(readl(base_addr + i * 4), iram_ddr_vars_base + i * 4);
		if(dump & DUMP_DDR_REGS)
			printk(KERN_DEBUG "cfg_id %d: [%08x] ddr_vars[%d]=%08x\n",
					cfg_id, phy_addr + i * 4,
					i, readl(iram_ddr_vars_base + i * 4));
	}
}

/* save mutiple ddr configs to iram */
void ddr_save(void)
{
	int i;

	for(i = 0; i < NR_DDR_CFG_ID; i++) {
		ddr_save_phase1(i);
		ddr_save_phase2(i);
	}
}
EXPORT_SYMBOL(ddr_save);

static unsigned long freq_switch_count = 0;

struct freq_table ddr_freq_table[] = {
	{ DDR_FREQ_533M, 533},
	{ DDR_FREQ_400M, 400},
	{ DDR_FREQ_333M, 333},
	{ DDR_FREQ_200M, 200},
	{ DDR_FREQ_END,	~0},
};

static void __back_from_sram(void)
{
	freq_switch_count++;
}

int ddr_switch_freq(unsigned int freq_mhz)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(ddr_freq_table); i++) {
		if(ddr_freq_table[i].index == DDR_FREQ_END)
			return -EINVAL;
		if(ddr_freq_table[i].freq != freq_mhz)
			continue;
	}

	writel(freq_mhz, sarram + SRAM_DFS_MHZ);
	writel(__back_from_sram, sarram + SRAM_BACK_ENTRY);

	/* jump to sarram */
	((void (*)())(readl(sarram)))();
	printk(KERN_DEBUG "freq_switch_count %ld\n", freq_switch_count);

	return 0;
}
EXPORT_SYMBOL(ddr_switch_freq);
#endif	/* TEST */

static int get_s3_mode(void)
{
	void __iomem *addr = sarram + SRAM_S3_MODE;
	return readl(addr);
}

int is_clkstop_mode(void)
{
	return get_s3_mode() & MODE_CLKSTOP;
}

void n7s3_set_mode(unsigned int mode, unsigned char ctrl)
{
#ifdef CONFIG_SUSPEND
	void __iomem *addr = sarram + SRAM_S3_MODE;
	unsigned int timer_wakeup_enable = 0, val = 0;

	if(ctrl & (CTRL_S3_INIT | CTRL_S3_CLEAR)) {
		writel(0, addr);
		return;
	}

	if(ctrl & CTRL_S3_SET) {
		val = readl(addr);
		/* bp_flight  & clkstop is privileged */
		if(val & (MODE_BP_FLIGHT | MODE_CLKSTOP))
			return;
	}

	if(ctrl & CTRL_S3_SET) {
		/* Using 12MHz as default */
		val = MODE_12M_CLK;

		if(mode & MODE_CLKSTOP)		/* 0x201 */
			val |= MODE_CLKSTOP | MODE_INTERNAL_SUS;

		else if(mode & MODE_BP_FLIGHT)	/* 0x201 */
			val |= MODE_BP_FLIGHT | MODE_INTERNAL_SUS;

		else if(mode & MODE_AP_NORMAL)	/* 0x201 */
			val |= MODE_AP_NORMAL | MODE_INTERNAL_SUS;
		else
			val |= mode;

		if(suspend_ctrl & (1 << 10))
			val |= MODE_DDR_ASR;
		if(suspend_ctrl & (1 << 9))
			val |= MODE_POLL_ASR;
		if(suspend_ctrl & (1 << 11))
			val |= MODE_CLK_WORKAROUND;
	} else
		val = mode;

	timer_wakeup_enable = (suspend_ctrl & 0x100) | wktimer_auto_wakeup;
	auto_wakeup = timer_wakeup_enable | rtc_auto_wakeup;
	if(timer_wakeup_enable) {
		unsigned int intvl = suspend_ctrl >> 12;
		/* sleep at least 300ms */
		intvl = (intvl > 300) ? intvl : 300;
		writel(intvl, sarram + SRAM_WKUP_INTVL);

		val |= MODE_WAKE_UP;
	}
	writel(val, addr);
#endif
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ehr_early_suspend(struct early_suspend *h)
{
	struct ehr_struct *ehr = container_of(h, struct ehr_struct, early_suspend);
	ehr->es_complete = 1;
	//ehr->lr_complete = !ehr->es_complete;
}

static void ehr_late_resume(struct early_suspend *h)
{
	struct ehr_struct *ehr = container_of(h, struct ehr_struct, early_suspend);
	ehr->lr_complete = 1;
	ehr->es_complete = !ehr->lr_complete;
	hrtimer_cancel(&ehr->timer);
}
#endif

static enum hrtimer_restart ehr_func(struct hrtimer *hr)
{
	struct ehr_struct *ehr = container_of(hr, struct ehr_struct, timer);
	int ret = HRTIMER_NORESTART;

	if((ehr->state == EHR_STATE_RESUME) && !ehr->lr_complete) {
		printk(KERN_ERR "XXX Bug in resume flow %ld(will retry)\n", ++ehr->lr_fail);
		if(ehr->lr_fail > MAX_RETRY)
			BUG();
		hrtimer_forward_now(&ehr->timer, ktime_set(ehr->interval * 2, 0));
		ret = HRTIMER_RESTART;
	} else {
		if(dump & DUMP_ESHRTIMER)
			printk(KERN_DEBUG "ehr test pass %ld times\n", ++ehr->pass);
		/* reset value */
		ehr->lr_fail = 0;
	}

	return ret;
}

static int __maybe_unused producer(struct debug_timer *dt)
{
	unsigned long flags;
	struct debug_timer *t = dt;

	/* just do it once, or u can use this as a mem leak generator */
	if(!t->stats.read_buf || !t->stats.write_buf || mem_leaker) {
		t->stats.read_buf = kmalloc(cfs_buf_sz, GFP_KERNEL);
		t->stats.write_buf = kmalloc(cfs_buf_sz, GFP_KERNEL);

		if(!t->stats.read_buf|| !t->stats.write_buf) {
			printk(KERN_ERR "buf not allocated?\n");
			return -ENOMEM;
		}
	}

	get_random_bytes(t->stats.read_buf, cfs_buf_sz);
	/* no reason to lock, we do this only for debug */
	spin_lock_irqsave(&t->lock, flags);
	memcpy(t->stats.write_buf, t->stats.read_buf, cfs_buf_sz);
	spin_unlock_irqrestore(&t->lock, flags);
	t->stats.crc16 = crc16(0, t->stats.write_buf, cfs_buf_sz);
	return 0;
}

static void consumer(struct debug_timer *dt)
{
	int ret;
	unsigned long flags;
	struct debug_timer *t = dt;

	spin_lock_irqsave(&t->lock, flags);
	ret = memcmp(t->stats.read_buf, t->stats.write_buf, cfs_buf_sz);
	spin_unlock_irqrestore(&t->lock, flags);

	if(ret) {
		printk(KERN_ERR "%s conflicted data(CPU%d-->CPU%d)\n",
				__func__, t->stats.sender, t->stats.recver);
		BUG();
	}

	WARN_ON(crc16(0, t->stats.read_buf, cfs_buf_sz) != t->stats.crc16);
}

static void debug_func(struct debug_timer *t, gfp_t gfp)
{
	int i, result = 0;
	unsigned int rv, sz;
	char test, pattern;
	char *src_buf, *dest_buf;

	if(dtf & T_RANDOM_TEST) {
		unsigned long flags;

		src_buf = t->src_buf;
		dest_buf = t->dest_buf;

		/* escape 1st round */
		if(src_buf && dest_buf) {
			/* compare data */
			result = memcmp(dest_buf, src_buf, t->buf_sz);
			if(result) {
				printk(KERN_ERR "Corrupted data!!! dest %p src %p\n",
								dest_buf, src_buf);
				BUG();
			}

			printk_once(KERN_DEBUG "Enter %s\n", __func__);
			kfree(src_buf);
			kfree(dest_buf);
		}

		if(t->buf_sz == 0 || (t->buf_sz == MAX_BUF_SIZE))
			sz = MAX_BUF_SIZE << 3;
		else
			sz = MAX_BUF_SIZE;

		src_buf = kmalloc(sz, gfp);
		dest_buf = kmalloc(sz, gfp);

		if(!src_buf || !dest_buf) {
			printk(KERN_ERR "buf not allocated?\n");
			return;
		}

		/* don't forget to update */
		t->buf_sz = sz;
		t->src_buf = src_buf;
		t->dest_buf = dest_buf;
		pattern = (char)(random_pattern & 0xFF);
		memset(src_buf, pattern, sz);
		smp_mb();
		get_random_bytes(&test, sizeof(char));
		for(i = 0; i < MAX_BUF_SIZE/0x100; i++) {
			rv = (test + (0x100 * i)) % MAX_BUF_SIZE;
			if(src_buf[rv] != pattern) {
				printk(KERN_ERR "%p: rv=%d, expected %x, but got %x\n",
							src_buf, rv, pattern, src_buf[rv]);
				BUG();
			}
		}
		/* updata data */
		get_random_bytes(src_buf, sz);
		spin_lock_irqsave(&t->lock, flags);

		memcpy(dest_buf, src_buf, sz);
		spin_unlock_irqrestore(&t->lock, flags);
	}

	if(dtf & T_EARLY_SUSPEND) {
		input_report_key(est_dev, KEY_POWER, 1);
		input_report_key(est_dev, KEY_POWER, 0);
		input_sync(est_dev);
	}

	if(dtf & T_USE_WORKQUEUE) {
		t->stats.wield_cpu[get_cpu()]++;
		put_cpu();
		printk(KERN_DEBUG "online %d, wield<%ld, %ld> "
				"pinned<%ld, %ld> "
				"missed<%ld, %ld>\n",
				num_online_cpus(), t->stats.wield_cpu[0], t->stats.wield_cpu[1],
				t->stats.work_pinned_cpu[0], t->stats.work_pinned_cpu[1],
				t->stats.queue_work_fail[0], t->stats.queue_work_fail[1]);
	}

#ifdef CONFIG_SMP
	if(dtf & T_SCF_TEST) {
		static unsigned int round_robin = 1;
		unsigned int sender, recver, type;

		recver = 0;
		sender = get_cpu();
		put_cpu();

		recver += round_robin;
		round_robin++;
		recver %= 4;

		/* use dynamic mode to skip online constrain */
		if(!cpu_online(recver) && !(call_func_mode & CFM_OFFLINE_TEST)) {
			/* last chance to reside in an specific cpu */
			recver = sender;
		}

		type = (sender << 2) + recver;
		t->stats.cfs_send[type]++;
		t->stats.cfs_type = type;
		t->stats.sender = sender;
		t->stats.recver = recver;

		/* let me do some stuff for other cpus */
		producer(t);

		/* cfs_wait controls wait or not, never ever put this in th */
		__smp_call_function_single(recver,
				(struct call_single_data *)&t->call_func_data,
					cfs_wait);
	}
#endif

	/*
	 * use a hw triggered method to update clock source,
	 * it works with shoestring price
	 */
#if 0
	if(dtf & T_UPDATE_SC)
		update_sched_clock();
#endif

	if(dtf & T_WAKELOCK_C) {
		int ret = 0;//has_wake_lock(WAKE_LOCK_SUSPEND);
		if(ret)
			printk(KERN_DEBUG "%s wakelock hold\n",
					(ret == -1) ? "infinite" : "timeout");
	}

	t->stats.test_cnt++;
	printk(KERN_DEBUG "%s%s%stest pass %ld times(%ld missed)\n",
			(dtf & T_RANDOM_TEST) ? "random " : "",
			(dtf & T_EARLY_SUSPEND) ? "early_suspend " : "",
			(dtf & T_UPDATE_SC) ? "sched_clock ": "",
			t->stats.test_cnt, (t->stats.irq_recv - t->stats.test_cnt));
}

static void debug_tasklet_func(unsigned long priv)
{
	struct debug_timer *t = (struct debug_timer *)priv;
	debug_func(t, GFP_ATOMIC);
}

static void debug_worker(struct work_struct *work)
{
	debug_func(container_of(work, struct debug_timer, work), GFP_KERNEL);
}

static inline unsigned int INC(unsigned int x)
{
	return x++;
}

/* this func must be non-blocking, and should be AFAP */
static void debug_smp_call_func(void *info)
{
	unsigned int this_cpu, type, i = 0;
	struct debug_timer *t = (struct debug_timer *)info;

	type = t->stats.cfs_type;
	this_cpu = smp_processor_id();
	WARN_ON(this_cpu != t->stats.recver);
	t->stats.cfs_handled[type]++;

	consumer(t);
	printk(KERN_DEBUG "CFS-RECV: <%ld %ld %ld %ld> <%ld %ld %ld %ld>"
				" <%ld %ld %ld %ld> <%ld %ld %ld %ld>\n",
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)],
				t->stats.cfs_handled[INC(i)], t->stats.cfs_handled[INC(i)]);
	i = 0;
	printk(KERN_DEBUG "CFS-SEND: <%ld %ld %ld %ld> <%ld %ld %ld %ld>"
				" <%ld %ld %ld %ld> <%ld %ld %ld %ld>\n",
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)],
				t->stats.cfs_send[INC(i)], t->stats.cfs_send[INC(i)]);

}

static irqreturn_t debug_irq_handler(int irq, void *dev_id)
{
	int this_cpu, cpu, ret;
	struct debug_timer *t = (struct debug_timer *)dev_id;
	//void __iomem* timer_va = timer1_base + TIMER_OFFSET * 2;
	void __iomem* timer_va = timer1_base + (irq - TIMER_IRQ_OFFSET) * TIMER_OFFSET;
	u32 val = readl(timer_va + TIMER_INTS);

	if(val & 0x1) {
		readl(timer_va + TIMER_EOI);
#ifdef TEST
		ddr_switch_freq(DDR_FREQ_400M);
#endif
	} else {
		printk(KERN_ERR "Spurious irq %d\n", irq);
		return IRQ_NONE;
	}

	if(dtf & T_SW_TIMER) {
		mod_timer(&t->swt, jiffies + msecs_to_jiffies(test_ms / 2));
		if(t->stats.swt_handled && !atomic_read(&t->stats.swt_touch))
			printk(KERN_ERR "swt miss %ld\n", ++t->stats.swt_miss);
		atomic_set(&t->stats.swt_touch, 0);
	}

	if(dtf & T_USE_WORKQUEUE) {
		if(wqctrl) {
			static unsigned int round_robin = 1;
			this_cpu = smp_processor_id();
			cpu = ++round_robin % 4;

			if(cpu == this_cpu)
				cpu = ++round_robin % 4;
			/* in case target cpu is offline */
			if(!cpu_online(cpu))
				cpu = this_cpu;
			ret = queue_work_on(cpu, t->wq, &t->work);
			t->cpu = !cpu;
		} else {
			cpu = smp_processor_id();
			ret = queue_work(t->wq, &t->work);
		}

		t->stats.work_pinned_cpu[cpu]++;
		if(!ret)
			t->stats.queue_work_fail[cpu]++;
	} else if (dtf & T_USE_TASKLET) {
		tasklet_schedule(&t->tasklet);
	}
	t->stats.irq_recv++;

	printk(KERN_DEBUG "timer irq %ld!\n", t->stats.irq_recv);
	printk_once(KERN_DEBUG "timer irq%d armed!\n", irq);
	return IRQ_HANDLED;
}

static void stop_debug_timer(void)
{
	struct debug_timer *t = debug_timer;
	if(t->requested) {
		if(dtf & T_USE_WORKQUEUE)
			flush_workqueue(t->wq);
		if(dtf & T_SW_TIMER)
			del_timer_sync(&t->swt);
		//free_irq(t->irq, t);

		if(t->src_buf)
			kfree(t->src_buf);
		if(t->dest_buf)
			kfree(t->dest_buf);
		if(t->stats.read_buf)
			kfree(t->stats.read_buf);
		if(t->stats.write_buf)
			kfree(t->stats.write_buf);

		memset(&t->stats, 0, sizeof(t->stats));
		t->requested  	= 0;
		t->cpu		= 0;
		t->buf_sz     	= 0;
		t->src_buf    	= NULL;
		t->dest_buf   	= NULL;
		writel(0, t->va + TIMER_CTRL);
		clk_disable(t->clk);
	}
}

/*
 * Make sure timer clock is enabled
 * prcm 0x058211b0 default value is 0x00ff01ff
 */
static int start_debug_timer(unsigned int id, unsigned int ms)
{
	int irq, ret = -1;
	void __iomem* timer_va;

	id &= 7;
	if(debug_timer->requested)
		return -EINVAL;

	irq = TIMER_IRQ_OFFSET;
	/*ret = request_irq(irq, debug_irq_handler, 0, S3_DRIVER_NAME, debug_timer);
	if(ret) {
		pr_err("Err %d: request irq %d\n", ret, irq);
		return -EINVAL;
	}

	if(apply_affinity)
		irq_set_affinity(irq, cpumask_of(apply_affinity & 3));
*/
	printk(KERN_DEBUG "enable irq%d, interval %dms\n", irq, ms);

	timer_va = timer_base + id * TIMER_OFFSET;
	debug_timer->irq = irq;
	debug_timer->ms = ms;
	debug_timer->va = timer_va;
	debug_timer->requested = 1;

	writel(0, timer_va + TIMER_CTRL);
	writel(_TIMER_USER_DEFINE/*_TIMER_INTEN*/, timer_va + TIMER_CTRL);
#if 1
	/*user defined periodic mode */
	writel(ms * TIMER_CLK_KHZ, timer_va + TIMER_LCNT);
	//writel(_TIMER_ENABLE|_TIMER_USER_DEFINE, timer_va + TIMER_CTRL);
#else
	/* free running mode */
	writel(0xFFFFFFFF, timer_va + TIMER_LCNT);
	writel(_TIMER_ENABLE, timer_va + TIMER_CTRL);
#endif
	return 0;
}

static void dump_cp15(void)
{
	unsigned int i = 0, x;
	unsigned int val[100];

	/* c0 registers */
	asm("mrc p15, 0, %0, c0, c0, 0 @Main ID"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 1 @Cache Type" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 2 @TCM Type" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 3 @TLB Type" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 4 @Alias of MIDR" 	: "=r" (val[INC(i)]): : "cc");	/* 1 - 5 */
	asm("mrc p15, 0, %0, c0, c0, 5 @MultiProcessor" : "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 6 @Revision ID" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c0, 7 @Alias of MIDR" 	: "=r" (val[INC(i)]): : "cc");

	asm("mrc p15, 0, %0, c0, c1, 0 @Processor Feature R0"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c1, 1 @Processor Feature R1"	: "=r" (val[INC(i)]): : "cc");	/* 6 - 10 */
	asm("mrc p15, 0, %0, c0, c1, 2 @Debug Feature R0"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c1, 3 @Auxiliary Feature R0"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c1, 4 @Memory Model Feature R0": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c1, 5 @Memory Model Feature R1": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c1, 6 @Memory Model Feature R2": "=r" (val[INC(i)]): : "cc");	/* 11 - 15 */
	asm("mrc p15, 0, %0, c0, c1, 7 @Memory Model Feature R3": "=r" (val[INC(i)]): : "cc");

	asm("mrc p15, 0, %0, c0, c2, 0 @Instruction Set Attr R0": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c2, 1 @Instruction Set Attr R1": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c2, 2 @Instruction Set Attr R2": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c2, 3 @Instruction Set Attr R3": "=r" (val[INC(i)]): : "cc");	/* 16 - 20 */
	asm("mrc p15, 0, %0, c0, c2, 4 @Instruction Set Attr R4": "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c0, c2, 5 @Instruction Set Attr R5": "=r" (val[INC(i)]): : "cc");

	asm("mrc p15, 1, %0, c0, c0, 0 @Cache Size ID"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 1, %0, c0, c0, 1 @Cache Level ID"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 1, %0, c0, c0, 7 @Auxiliary ID"	: "=r" (val[INC(i)]): : "cc");	/* 21 - 25 */

	asm("mrc p15, 2, %0, c0, c0, 0 @Cache Size Slection"		: "=r" (val[INC(i)]): : "cc");
#if 0
	asm("mrc p15, 4, %0, c0, c0, 0 @Virtualization Processor ID"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c0, c0, 5 @Virtual Multiprocessor ID"	: "=r" (val[INC(i)]): : "cc");
#endif
	/* c1 registers */
	asm("mrc p15, 0, %0, c1, c0, 0 @System Control"		: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c1, c0, 1 @Auxiliary Control"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c1, c0, 2 @Coprocessor Access Ctrl": "=r" (val[INC(i)]): : "cc");

	asm("mrc p15, 0, %0, c1, c1, 0 @Secure Configurateion"	: "=r" (val[INC(i)]): : "cc");	/* 26 - 30 */
	asm("mrc p15, 0, %0, c1, c1, 1 @Secure Dbg Enable"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c1, c1, 2 @Non-Secure Access Ctrl"	: "=r" (val[INC(i)]): : "cc");

#if 0
	asm("mrc p15, 4, %0, c1, c0, 0 @Hyp System Ctrl"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c1, c0, 1 @Hyp Auxiliary Ctrl"	: "=r" (val[INC(i)]): : "cc");

	asm("mrc p15, 4, %0, c1, c1, 0 @Hyp Configuration"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c1, c1, 1 @Hyp Dbg Ctrl"		: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c1, c1, 2 @Hyp Coprocessor Trap"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c1, c1, 3 @Hyp System Trap"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c1, c1, 7 @Hyp Coprocessor Trap"	: "=r" (val[INC(i)]): : "cc");
#endif

	/* c2 registers */
	asm("mrc p15, 0, %0, c2, c0, 0 @TTBR0"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c2, c0, 1 @TTBR1"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c2, c0, 2 @TTBCR"	: "=r" (val[INC(i)]): : "cc");	/* 31 - 35 */
#if 0
	asm("mrc p15, 4, %0, c2, c0, 2 @Hyp Translation Ctrl"	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c2, c1, 2 @Virt Translation Ctrl"	: "=r" (val[INC(i)]): : "cc");
#endif

	/* c3 registers */
	asm("mrc p15, 0, %0, c3, c0, 0 @Domain Access Ctrl"	: "=r" (val[INC(i)]): : "cc");

	/* c4 registers -- none */
	/* c5 registers -- data, instr fault status registers */
	/* c6 registers -- data, instr fault address registers */

	/* c7 registers */

	/* c8 registers TLB */

	/* c9 registers PMU */

	/* c10 registers */
	asm("mrc p15, 0, %0, c10, c2, 0 @Primary Region Remap Register" : "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c10, c2, 1 @Normal Memory Remap Register" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c10, c3, 0 @Aux Mem Attr Indirection R0" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c10, c3, 1 @Aux Mem Attr Indirection R1" 	: "=r" (val[INC(i)]): : "cc");
#if 0
	asm("mrc p15, 4, %0, c10, c2, 0 @Hyp Mem Attr Indirection R0" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c10, c2, 1 @Hyp Mem Attr Indirection R1" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c10, c3, 0 @HypAuxMemAttrIndirection R0" 	: "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 4, %0, c10, c3, 1 @HypAuxMemAttrIndirectionR1" 	: "=r" (val[INC(i)]): : "cc");
#endif
	/* c11 registers -- none */

	/*
	 * c12 registers
	 */
	asm("mrc p15, 0, %0, c12, c0, 0 @Vector Base Addr" 	: "=r" (val[INC(i)]): : "cc");	/* 36 - 40 */
	asm("mrc p15, 0, %0, c12, c0, 1 @MonitorVectorBaseAddr" : "=r" (val[INC(i)]): : "cc");
	asm("mrc p15, 0, %0, c12, c1, 0 @Interrupt Status" 	: "=r" (val[INC(i)]): : "cc");

	//asm("mrc p15, 4, %0, c12, c0, 0 @Hyp Vector Base Addr" 	: "=r" (val[INC(i)]): : "cc");

	/* c13 registers -- Fast Context Switch */

	/* c14 registers -- Generic Timer */
	/* c15 registers */

	for(x = 0; x < 3; x++) {
		asm("mcr p15, 2, %0, c0, c0, 0" : : "r" (x): "cc");
		asm("isb");
		asm("mrc p15, 1, %0, c0, c0, 0" : "=r" (val[INC(i)]): : "cc");
	}

	for(; i < 100;)
		val[INC(i)] = 0xDEADdead;
	for(i = 0; i < 100;)
		printk(KERN_DEBUG "[%.3d] %08x %08x %08x %08x\n",
					i, val[INC(i)], val[INC(i)], val[INC(i)], val[INC(i)]);

}

struct proc_dir_entry *s3_dir;

static ssize_t s3_ctrl_read(struct file *filp, char __user *buffer, size_t count, loff_t *pos)
{
	count = sprintf(buffer, "%s\n", "normal flight clkstop clear timer stop");

	return count;
}

static ssize_t s3_ctrl_write(struct file *file, const char __user *buffer,
						size_t count, loff_t *pos)
{
	int len;
	char *str = NULL;

	if(count > MAX_LEN || count < MIN_LEN)
		return -EINVAL;

	str = kmalloc(count, GFP_KERNEL);
	if(!str)
		return -ENOMEM;

	if(copy_from_user(str, buffer, count)) {
		kfree(str);
		return -EFAULT;
	}
	str[count - 1] = '\0';

	printk(KERN_DEBUG "%s len = %d count = %d\n", str, len = count - 1, count);

	if(len == 6 && !strncmp("normal", str, len)) {
		n7s3_set_mode(MODE_AP_NORMAL, CTRL_S3_SET);
#if 0
	} else if(len == 6 && !strncmp("flight", str, len)) {
		n7s3_set_mode(MODE_BP_FLIGHT, CTRL_S3_SET);
#endif
	} else if(len == 7 && !strncmp("clkstop", str, len)) {
		n7s3_set_mode(MODE_CLKSTOP, CTRL_S3_SET);
	} else if(len == 5 && !strncmp("clear", str, len)) {
		n7s3_set_mode(0, CTRL_S3_CLEAR);
	} else if(!strncmp("timer", str, 5)) {
		int num,ret,index=0;
		index = 5;
		while(!isdigit(*(str+index)) && *(str+index) != '\0')
			index++;
		ret=kstrtou32(str+index,10,&num );
		if( ret ){
			printk("parameter err! ret=%d,%s\n",ret,str+index);
			num =10;
		}
		WakeupTimerSetClk32K(test_id);
		start_debug_timer(test_id, test_ms*num);
	} else if(len == 4 && !strncmp("stop", str, len)) {
		WakeupTimerClrClk32K(test_id);
		stop_debug_timer();
	} else if(len == 4 && !strncmp("cp15", str, len)) {
		dump_cp15();
	} else {
		kfree(str);
		return -EINVAL;
	}

	kfree(str);
	/* count rather than len */
	return count;
}

static int s3_pm_notifier(struct notifier_block *notifier,
				unsigned long pm_event, void *v)
{
	char *event_str = NULL;

	switch(pm_event) {
	case PM_SUSPEND_PREPARE:
		event_str = "pm_suspend_prepare";
		break;
	case PM_POST_SUSPEND:
		event_str = "pm_suspend_post";
		break;
	}

	printk(KERN_DEBUG "%s %s\n", __func__, event_str);
	return NOTIFY_OK;
}

static struct notifier_block s3_pm_nb = {
	.notifier_call = s3_pm_notifier,
};

static int idle_notifier(struct notifier_block *nb,
			unsigned long val,
			void *data)
{
	switch(val) {
	case IDLE_START:
		WARN_ON(irqs_disabled());
		break;
	case IDLE_END:
		break;
	}
	return 0;
}

static struct notifier_block idle_nb = {
	.notifier_call = idle_notifier,
};

void swt_func(unsigned long data)
{
	struct debug_timer *t = (struct debug_timer *)data;
	struct timespec ts;
	struct rtc_time tm;

	if(atomic_read(&t->stats.swt_touch)){
		printk(KERN_ERR "atomic buggy\n");
	}

	atomic_set(&t->stats.swt_touch, 1);
	getnstimeofday(&ts);
	rtc_time_to_tm(ts.tv_sec, &tm);
	printk(KERN_DEBUG "UTC: %d-%02d-%02d %02d:%02d:%02d.%09lu(%ld)\n",
			tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec,
						++t->stats.swt_handled);
}

static const struct file_operations s3_ctrl_fops = {
	.owner		= THIS_MODULE,
	.read		= s3_ctrl_read,
	.write		= s3_ctrl_write,
};

static int s3_probe(struct platform_device *pdev)
{
	int error = -ENXIO;
	struct ehr_struct *ehr;
	struct proc_dir_entry *ent = NULL;
//	struct regulator *core_regulator;

	get_cpu_ppn();
	s3_dir = proc_mkdir(S3_DRIVER_NAME, NULL);

	ent = proc_create_data("ctrl", 0644, s3_dir, &s3_ctrl_fops, NULL);
	if(!ent)
		return -ENOMEM;

#ifdef TEST
	construct_ddr_cfg(1);
	ddr_save();
#endif
	//timer1_base = ioremap(TIMER1_BASE, 0x100);
	sarram = __io_address(SARRAM_BASE);
	timer_base = ioremap(TIMER_BASE, 0x100);

	if(!timer_base || !sarram)
		return -ENOMEM;

	debug_timer = kzalloc(sizeof(struct debug_timer), GFP_KERNEL);
	if(!debug_timer) {
		printk(KERN_ERR "kmalloc fail\n");
		goto fail;
	}

	if(dtf & T_USE_WORKQUEUE) {
		debug_timer->wq = create_workqueue("debug_timer_wq");
		if(!debug_timer->wq) {
			printk(KERN_ERR "create_workqueue fail\n");
			goto fail;
		}
		INIT_WORK(&debug_timer->work, debug_worker);
	} else if(dtf & T_USE_TASKLET) {
		tasklet_init(&debug_timer->tasklet, debug_tasklet_func,
						(unsigned long)debug_timer);
	}

	if(dtf & T_SCF_TEST) {
		debug_timer->call_func_data.flags = 0;
		debug_timer->call_func_data.func = debug_smp_call_func;
		debug_timer->call_func_data.info = (void *)debug_timer;
		spin_lock_init(&debug_timer->lock);
	}

	if(1/*(dtf & T_EARLY_SUSPEND) || auto_wakeup*/) {
		est_dev = input_allocate_device();
		if(!est_dev)
			goto fail;
		input_set_capability(est_dev, EV_KEY, KEY_POWER);
		est_dev->name = "est";
		est_dev->phys = "est/input0";
		if(input_register_device(est_dev)) {
			error = -ENODEV;
			goto fail;
		}
	}

	if(dtf & T_ESHRTIMER) {
		debug_timer->ehr = ehr = kzalloc(sizeof(struct ehr_struct), GFP_KERNEL);
		if(!ehr)
			goto fail;

		hrtimer_init(&ehr->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ehr->timer.function = ehr_func;
#ifdef CONFIG_HAS_EARLYSUSPEND
		ehr->early_suspend.suspend = ehr_early_suspend;
		ehr->early_suspend.resume = ehr_late_resume;
		register_early_suspend(&ehr->early_suspend);
#endif
	}

	if(dtf & T_PMN_TEST)
		register_pm_notifier(&s3_pm_nb);

	if(dtf & T_SW_TIMER) {
		init_timer(&debug_timer->swt);
		debug_timer->swt.function = swt_func;
		debug_timer->swt.data = (unsigned long)debug_timer;
	}

	if(0/*dtf & T_CPUIDLE*/)
		idle_notifier_register(&idle_nb);

	platform_set_drvdata(pdev, debug_timer);

	return 0;
fail:
	if(debug_timer) {
		if(debug_timer->wq)
			destroy_workqueue(debug_timer->wq);
		if(debug_timer->ehr)
			kfree(debug_timer->ehr);
		kfree(debug_timer);
	}

	if(est_dev) {
		input_free_device(est_dev);
		kfree(est_dev);
	}

	platform_set_drvdata(pdev, NULL);
	return error;
}

static int s3_remove(struct platform_device *pdev)
{
	remove_proc_entry("ctrl", s3_dir);

	if(dtf)
		destroy_workqueue(debug_timer->wq);

	if(dtf & T_EARLY_SUSPEND)
		input_unregister_device(est_dev);

	if(dtf & T_ESHRTIMER) {
		hrtimer_cancel(&debug_timer->ehr->timer);
#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&debug_timer->ehr->early_suspend);
#endif
		kfree(debug_timer->ehr);
	}
	if(dtf & T_PMN_TEST)
		unregister_pm_notifier(&s3_pm_nb);

	if(debug_timer)
		kfree(debug_timer);

	return 0;
}

#ifdef CONFIG_PM
static int load_s3fw(struct device *dev)
{
	int ret = 0;
	const struct firmware *fw = NULL;
	const char * fw_name = "n7s3_fw_ref.bin";

	if(is_clkstop_mode()) {
		if(g_cpu_ppn == ARM_CA9) {
			char clkstop_fw_name[24];
			int len = strlen("n7s3_fw");
			strncpy(clkstop_fw_name, fw_name, len);
			clkstop_fw_name[len] = '\0';
#define CLKSTOP_FW_NAME		"_clkstop.bin"
			strncat(clkstop_fw_name, CLKSTOP_FW_NAME, strlen(CLKSTOP_FW_NAME));
			fw_name = clkstop_fw_name;
		}

	}

	ret = request_firmware(&fw, fw_name, dev);
	if(ret)	{
		printk(KERN_ERR "load %s failed", fw_name);
		return ret;
	}
	if(fw->size < 0x2000) {
		memcpy(sarram, fw->data, fw->size);
		ret = 0;
	} else {
		printk(KERN_ERR "%s %d too large to load", fw_name, fw->size);
		ret = -EINVAL;
	}

	release_firmware(fw);
	return ret;
}

static int s3_pm_prepare(struct device *dev)
{
	struct debug_timer *t = dev_get_drvdata(dev);

	if(dtf & T_ESHRTIMER) {
		t->ehr->state = EHR_STATE_SUSPEND;
		t->ehr->lr_fail = 0;
	}
	return (flow_buggy & FLOW_PREPARE) ? -1 : 0;
}

static int s3_pm_suspend(struct device *dev)
{
	return (flow_buggy & FLOW_SUSPEND) ? -1 : 0;
}

static int s3_pm_suspend_late(struct device *dev)
{
	return (flow_buggy & FLOW_SUSPEND_LATE) ? -1 : 0;
}

static int s3_pm_suspend_noirq(struct device *dev)
{
	load_s3fw(dev);
	return (flow_buggy & FLOW_SUSPEND_NOIRQ) ? -1 : 0;
}

static int s3_pm_resume_noirq(struct device *dev)
{
	return (flow_buggy & FLOW_RESUME_NOIRQ) ? -1 : 0;
}

static int s3_pm_resume_early(struct device *dev)
{
	return (flow_buggy & FLOW_RESUME_EARLY) ? -1 : 0;
}

static int s3_pm_resume(struct device *dev)
{
	return (flow_buggy & FLOW_RESUME) ? -1 : 0;
}

static void s3_pm_complete(struct device *dev)
{
	struct debug_timer *t = dev_get_drvdata(dev);

	if(t->requested) {
		writel(0, t->va + TIMER_CTRL);
		writel(_TIMER_USER_DEFINE/*_TIMER_INTEN*/, t->va + TIMER_CTRL);
		writel(t->ms * TIMER_CLK_KHZ, t->va + TIMER_LCNT);
		/*writel(_TIMER_ENABLE|_TIMER_USER_DEFINE, t->va + TIMER_CTRL);

		if(apply_affinity)
			irq_set_affinity(t->irq, cpumask_of(1));*/
	}

	if(dtf & T_ESHRTIMER) {
		t->ehr->state = EHR_STATE_RESUME;
		t->ehr->lr_complete = 0;
		t->ehr->interval = ehr_interval;
		/*
		 * This may trigger a wrong event if suspend fails,
		 * in this case resume callback will be called subsequently,
		 * if it reaches here, wrong event will be captured.
		 * This hanppens in real world, so just ignore this
		 * in such conditions.
		 */
		hrtimer_start(&t->ehr->timer,
				ktime_set(t->ehr->interval, 0),
				HRTIMER_MODE_REL);
	}

	if(suspend_done) {
		input_report_key(est_dev, KEY_POWER, 1);
		input_report_key(est_dev, KEY_POWER, 0);
		input_sync(est_dev);
		suspend_done = 0;
	}

	if(auto_wakeup) {
		int unblank = 1;
		if(screen_test) {
			if(screen_test_cnt++ % 5 != 0)
				unblank = 0;
		}

		if(unblank) {
			if(suspend_done) {
				input_report_key(est_dev, KEY_POWER, 1);
				input_report_key(est_dev, KEY_POWER, 0);
				input_sync(est_dev);
				suspend_done = 0;
			} else
				printk(KERN_DEBUG "suspend failed %ld times\n", ++suspend_fail);
		}
	}
}

static struct dev_pm_ops s3_pm_ops = {
	.prepare	= s3_pm_prepare,
	.suspend	= s3_pm_suspend,
	.suspend_late	= s3_pm_suspend_late,
	.suspend_noirq	= s3_pm_suspend_noirq,
	.resume_noirq	= s3_pm_resume_noirq,
	.resume_early	= s3_pm_resume_early,
	.resume		= s3_pm_resume,
	.complete	= s3_pm_complete,
};
#endif	/* CONFIG_PM */

struct platform_driver s3_driver = {
	.probe	= s3_probe,
	.remove	= s3_remove,
	.driver	= {
		.name	= S3_DRIVER_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &s3_pm_ops,
#endif
	},
};

struct platform_device s3_device = {
	.name	= S3_DRIVER_NAME,
	.id	= -1,
};

static int __init n7s3_init(void)
{
	int ret;
	printk("n7s3_init %s:%d\n", __FILE__, __LINE__);
	ret = platform_driver_register(&s3_driver);
	if(!ret) {
		ret = platform_device_register(&s3_device);
		if(ret)
			platform_driver_unregister(&s3_driver);
	}

	return ret;
}

static void __exit n7s3_exit(void)
{
	platform_driver_unregister(&s3_driver);
	platform_device_unregister(&s3_device);
}

static int __init debug_test(char *str)
{
	if(strstr(str, "tl"))
		dtf = T_USE_TASKLET;
	else if(strstr(str, "wq"))
		dtf = T_USE_WORKQUEUE;

	if(strstr(str, "rtf"))
		dtf |= T_RANDOM_TEST;
	if(strstr(str, "est"))
		dtf |= T_EARLY_SUSPEND;
	if(strstr(str, "ehr"))
		dtf |= T_ESHRTIMER;
	if(strstr(str, "pmn"))
		dtf |= T_PMN_TEST;
	return 0;
}
__setup("dtf=", debug_test);

module_init(n7s3_init);
module_exit(n7s3_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Li Le");
MODULE_DESCRIPTION("Suspend Control for NU7/NU7T series");

unsigned int wakeup_by_bp = 0;
unsigned int wakeup_by_baseband(void)
{
	int real;
	real = wakeup_by_bp;
	wakeup_by_bp = 0;
	return real;
}
EXPORT_SYMBOL(wakeup_by_baseband);

#ifdef CONFIG_SUSPEND
#define RAMC_BUF_SZ	(PAGE_SIZE * 300)
#define RAMC_BUF2_SZ	(PAGE_SIZE * 600)
#define RAMC_BUF3_SZ	(PAGE_SIZE * 900)

void *ramv_buf1 = NULL;
void *ramv_buf2 = NULL;
void *ramv_buf3 = NULL;

struct indicator_struct {
	unsigned long phys_off;
	unsigned long page_off;
	unsigned long fwbin;
} indicator;

#ifdef N7S3_TEST
#define READL(a)		(*((volatile unsigned int*)(a)))
#define WRITEL(v, a)		(*((volatile unsigned int *)(a)) = (v))

extern void ll_control(struct indicator_struct *i);
void __idmap ll_control(struct indicator_struct *i)
{
	unsigned long val, val2;

	if(g_cpu_ppn == ARM_CA9) {
		WRITEL(0, SCU_BASE + 0);

		val = READL(L220_BASE + 0);
		val |= 0x1;
		WRITEL(val, L220_BASE + 0);
		val2 = 0xFFFFE00;
	} else
		val2 = 0xFFFF8000;

	/* read Configuration Base Address Register */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r"(val) : : "cc");
	val &= val2;

	WRITEL(0, val + OFFSET_GIC_DIST_IF);
	WRITEL(0, val + OFFSET_GIC_CPU_IF);

	WRITEL(0x3, SARRAM_BASE + SRAM_SYS_STAT);

	val2 = offsetof(struct indicator_struct, fwbin);
	__asm__ __volatile__(
	"ldr	%0, [%1, %2]\n"
	"mov	pc, %0"
		: "=&r" (val)
		: "r" (i), "Ir" (val2)
		: "cc");
}

void __idmap __n7s3_cpu_resume_test(void)
{
	/* VGA power, pinmux */
	WRITEL(0x30, 0x05822018);
	WRITEL(0x4000, 0x06140000);
	WRITEL(0x4000, 0x06140004);

	/* PLL config */

	/* controller clock */
	WRITEL(0x10712, 0x05821204);
	WRITEL(0x10, 0x05821208);

	/* config controller */
	WRITEL(0x0c028060, 0x05010008);
	WRITEL(0x05820002, 0x0501000c);
	WRITEL(0x80000000, 0x05010010);		/* ptr */
	WRITEL(0x80100000, 0x05010014);
	WRITEL(0x80200000, 0x05010018);

	WRITEL(0x01e00280, 0x0501001c);
	WRITEL(0x0080c700, 0x05010020);
	WRITEL(0x00000002, 0x05010028);

	WRITEL(0x00000002, 0x05010040);
	WRITEL(0x0000ff00, 0x05010048);
	WRITEL(0x0000c480, 0x0501004c);

	WRITEL(0x02010100, 0x05010050);
	WRITEL(0x00000100, 0x05010054);

	/* enable display controller */
	WRITEL(0x00000980, 0x05010000);
	//WRITEL(0x00000880, 0x05010000);
	while(1);
}
#endif

static unsigned int g_pwr_ctrl, g_diag_reg;

static void __maybe_unused save_cpu_arch_register(void)
{
	if(g_cpu_ppn == ARM_CA9) {
		/*read power control register*/
		asm("mrc p15, 0, %0, c15, c0, 0" : "=r"(g_pwr_ctrl) : : "cc");
		/*read diagnostic register*/
		asm("mrc p15, 0, %0, c15, c0, 1" : "=r"(g_diag_reg) : : "cc");
	}
	return;
}

static void __maybe_unused restore_cpu_arch_register(void)
{
	if(g_cpu_ppn == ARM_CA9) {
		/*write power control register*/
		asm("mcr p15, 0, %0, c15, c0, 0" : : "r"(g_pwr_ctrl) : "cc");
		/*write diagnostic register*/
		asm("mcr p15, 0, %0, c15, c0, 1" : : "r"(g_diag_reg) : "cc");
	}
	return;
}

static void enable_wktimer(bool enable)
{
	unsigned int ret, val, wktimer = 0x4;

	ret = nufront_prcm_read(&val,  WAKEUP_IRQ_CTRL1);
	if(ret != 0)
		return;

	if(enable)
		val |= wktimer;
	else
		val &= ~wktimer;

	nufront_prcm_write(val, WAKEUP_IRQ_CTRL1);
}

static void show_wakeup_events(bool escape)
{
	u32 cgcapable;
	unsigned int which, val, ret;
	static unsigned long wkevt_stats[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	const char *wkevt_str[] = {"eth0", "wkgpio", "wktimer", "coregpio", "eth1", "eth2", "u2_dev", "u2_h1", "u2_h0", "u3_dev", "u3_host", };

	if(escape)
		return;

	ret = nufront_prcm_read(&val, WAKEUP_IRQ_STATUS);
	if(ret != 0)
		return;

	nufront_prcm_write(val, WAKEUP_IRQ_STATUS);
	printk(KERN_DEBUG "wakeup irq status=0x%08x\n", val);

	nufront_prcm_read(&cgcapable, WAKEUP_IRQ_CTRL1);
	cgcapable &= 8;

	val &= 0x7ff;
	if(!cgcapable)
		val &= ~(1 << 3);
#if 1
	/*
	 * PRCM just catch an edge, for irqs which are level triggered,
	 * this warning will show up in cases level irqs are pending but
	 * wakeup irq status are cleared before entering sleep.
	 * If you see this warning, better check error occured before sleep
	 * entry.
	 */
	//WARN_ON(val == 0);
	which = ffs(val) - 1;
	if(!cgcapable && (which == 3))
		which = 0;
	wkevt_stats[which]++;
	wakeup_by_bp = !which;

	printk(KERN_ALERT "wakeup event: %s\n", wkevt_str[which]);
#endif
	printk(KERN_ALERT "wakeup stats: eth0=%ld, wkgpio=%ld, wktimer=%ld, coregpio=%ld, eth1=%ld, eth2=%ld," \
						"u2_dev=%ld, u2_h1=%ld, u2_h0=%ld, u3_dev=%ld, u3_host=%ld\n",
					wkevt_stats[0], wkevt_stats[1], wkevt_stats[2], wkevt_stats[3], wkevt_stats[4],
					wkevt_stats[5], wkevt_stats[6], wkevt_stats[7], wkevt_stats[8], wkevt_stats[9],
					wkevt_stats[10]);
}

extern int __n7s3_cpu_sleep(unsigned long arg);
extern int __n7s3_cpu_resume(unsigned long arg);

static int n7s3_begin(suspend_state_t state)
{
	printk(KERN_DEBUG "%s suspend_state_t 0x%08x\n", __func__, state);
	//regulator_suspend_prepare(state);
	return 0;
}

#define BITS_4_PLL		0x700
/* found on small capsulation nu7t SoCs */
#define BITS_4_SDMMC		((3 << 5) | (3 << 13) | (3 << 21))
#define BITS_4_I2S		(7 << 16)
#define BITS_4_LCD		(7 << 8)
/* apply to both small and big capsulation nu7t SoCs */
#define BITS_4_NFC		((1 << 4) | (7 << 8))

//#define CLK_LIM_OLD_WA
//#define CAPSL_S
#ifdef CLK_LIM_OLD_WA
static unsigned int clk_limitation_workaround(unsigned int offset, unsigned int bits)
{
	unsigned int v;
	nufront_prcm_read(&v, offset);
	nufront_prcm_write(v | bits, offset);
	return v;
}
#else
static unsigned int v[5] = {0, 0, 0, 0, 0};
static /*noinline*/ __always_inline void clk_limitation_workaround(bool restore)
{
	if(!restore) {
#ifdef CAPSL_S
		nufront_prcm_read(&v[0], 0x80);
		nufront_prcm_write(v[0] | BITS_4_PLL, 0x80);
		nufront_prcm_read(&v[1], 0x184);
		nufront_prcm_write(v[1] | BITS_4_SDMMC, 0x184);
		nufront_prcm_read(&v[2], 0x200);
		nufront_prcm_write(v[2] | BITS_4_I2S, 0x200);
		//v[3] = nufront_prcm_readl(0x204);
		//nufront_prcm_write(v[3] | BITS_4_LCD, 0x204);
#endif
		v[4] = nufront_prcm_read(&v[4], 0x180);
		nufront_prcm_write(v[4] | BITS_4_NFC, 0x180);
	} else {
#ifdef CAPSL_S
		nufront_prcm_write(v[0], 0x80);
		nufront_prcm_write(v[1], 0x184);
		nufront_prcm_write(v[2], 0x200);
		//nufront_prcm_write(v[3], 0x204);
#endif
		nufront_prcm_write(v[4], 0x180);
	}
}
#endif

void WakeupTimerSetClk32K(int num) {
	int val;
	nufront_prcm_read(&val,WAKRUP_TIMER_CLK_CTRL);
	nufront_prcm_write( val & ~((0x1<<(num+16))), WAKRUP_TIMER_CLK_CTRL ); //timer reset

	nufront_prcm_read(&val, WAKRUP_TIMER_CLK_CTRL );
	nufront_prcm_write( val| (0x1<<(num+16)), WAKRUP_TIMER_CLK_CTRL ); //timer reset

	nufront_prcm_read(&val, WAKRUP_TIMER_CLK_CTRL );
	nufront_prcm_write( val | (0x1<<(num+8)), WAKRUP_TIMER_CLK_CTRL );

	nufront_prcm_read(&val, WAKRUP_TIMER_CLK_CTRL );
	nufront_prcm_write( val | (0x1<<num), WAKRUP_TIMER_CLK_CTRL );
}


void WakeupTimerClrClk32K(int num) {
	int val;
	nufront_prcm_read(&val, WAKRUP_TIMER_CLK_CTRL);
	nufront_prcm_write( val & (~(0x1<<(num+8))), WAKRUP_TIMER_CLK_CTRL );
	//  REG_WRITE(PRCM_TIMERWKUP_CLK_CTRL, REG_READ(PRCM_TIMERWKUP_CLK_CTRL) & (~(0x1<<num)) );
}

u32 exit_s3 = 0;

static int n7s3_enter(suspend_state_t state)
{
	bool ramv, memfree, escape = false;
	unsigned int val, wa, suspend_resume_ctrl;
	unsigned long flags;
	phys_addr_t where;
#ifdef CONFIG_ARM_ARCH_TIMER
        u32 syscounter_freq = 24000000;
#endif
	wa = suspend_ctrl & 8;
	suspend_resume_ctrl = suspend_ctrl & 0x7;

	ramv = ramv_buf1 && ramv_buf2 && (suspend_ctrl & 0x80);
	memfree = ramv_buf1 && ramv_buf2 && (suspend_ctrl & 0x80);

	suspend_done = 1;
	wakeup_by_bp = 0;
	if(ramv) {
		//get_random_bytes(ramv_buf1, RAMC_BUF_SZ);
		memset(ramv_buf1, 0x55, RAMC_BUF_SZ);
		memset(ramv_buf2, 0x55, RAMC_BUF2_SZ);
		memset(ramv_buf3, 0x55, RAMC_BUF3_SZ);
		printk(KERN_DEBUG "ramv_buf1: 0x%08x: 0x%08x\n", ((u32 *)ramv_buf1)[0], ((u32 *)ramv_buf1)[1]);
	}

	if(wa) {
		printk(KERN_DEBUG "wa %s:%s:%d\n", __FILE__, __func__, __LINE__);
#ifdef CLK_LIM_OLD_WA
		unsigned int v[5] = {0, 0, 0, 0, 0};
#ifdef CAPSL_S
		/* pll */
		v[0] = clk_limitation_workaround(0x80, BITS_4_PLL);
		/* sdmmc */
		v[1] = clk_limitation_workaround(0x184, BITS_4_SDMMC);
		/* i2s */
		v[2] = clk_limitation_workaround(0x200, BITS_4_I2S);
		/* lcd */
		v[3] = clk_limitation_workaround(0x204, BITS_4_LCD);
#endif
		/* nand */
		v[4] = clk_limitation_workaround(0x180, BITS_4_NFC);
#else
		clk_limitation_workaround(false);
#endif
	}
	dump_priv_regs(get_prcm_base(), 140, "prcm0", 0);

	/*
	 * we have 5 paths actually
	 * 0: do nothing;
	 * 1: this is clkstop mode;
	 * 2: 1st shortcut for suspend-resume;
	 * 3: 2nd shortcut for suspend-resume;
	 * 4: this is the normal mode for suspend-resume;
	 */
	switch(suspend_resume_ctrl) {
		case 1:
			indicator.fwbin = SARRAM_BASE;
			n7s3_set_mode(MODE_CLKSTOP, CTRL_S3_SET);
			break;
		case 2:
			escape = true;
			indicator.fwbin = virt_to_phys(cpu_resume);
			break;
		case 3:
			escape = true;
			indicator.fwbin = virt_to_phys(__n7s3_cpu_resume);
			break;
		case 4:
			indicator.fwbin = SARRAM_BASE;
			n7s3_set_mode(MODE_AP_NORMAL, CTRL_S3_SET);
			break;
		case 5:
			indicator.fwbin = SARRAM_BASE;
			n7s3_set_mode(random_pattern, 0);
			break;
	}

	val = readl(sarram + SRAM_S3_MODE);
	printk(KERN_ALERT "%s mode(%x), scheme: %d + %d\n",
			(val & MODE_AP_NORMAL) ? "s3" : "clkstop",
			val, suspend_resume_ctrl, resume_path);

#ifdef N7S3_TEST
	if(resume_path)
		where = virt_to_phys(__n7s3_cpu_resume_test);
	else
#endif
		where = virt_to_phys(__n7s3_cpu_resume);
		//where = virt_to_phys(cpu_resume);

	writel(where, sarram + SRAM_RESUME_ENTRY);
	writel(state, sarram + SRAM_SYS_STAT);
	if(ramv) {
		writel(virt_to_phys(ramv_buf1), sarram + SRAM_DDR_MEMCMP);
		writel(virt_to_phys(ramv_buf2), sarram + SRAM_DDR_MEMCMP2);
		writel(virt_to_phys(ramv_buf3), sarram + SRAM_DDR_MEMCMP3);
	}

	local_irq_save(flags);
	wmb();

	/* huge, or call your moan */
	outer_flush_all();
	flush_cache_all();
	outer_disable();
	dsb();

	if( debug_timer->requested )
		writel(_TIMER_ENABLE|_TIMER_USER_DEFINE, debug_timer->va + TIMER_CTRL);
	if(suspend_resume_ctrl)
		cpu_suspend((unsigned long)&indicator, __n7s3_cpu_sleep);

	printk(KERN_INFO "%s:%s:%d\n", __FILE__, __func__, __LINE__);

#if 0
	if(ramv) {
		err = 0;
		for(i =0; i < (RAMC_BUF3_SZ/4); i++) {
				if(((u32 *)ramv_buf3)[i] == 0x55555555) {
					continue;
				}
				else {
					err++;
					printk("%s:%d: ramv buf3 validation fails!!! index %d err: %d val:0x%x\n", __func__, __LINE__, i, err, ((u32*)ramv_buf3)[i]);
				}
		}
		if(err == 0)
			printk("%s: ramv buf3 validation ok!!! line:%d index %d\n", __func__, __LINE__, i);

		err = 0;
		for(i =0; i < (RAMC_BUF_SZ/4); i++) {
			if(((u32 *)ramv_buf1)[i] == 0x55555555) {
				continue;
			}
			else {
				err++;
				printk("%s:%d:ramv buf validation fails!!! index %d err: %d val:0x%x\n", __func__, __LINE__, i, err, ((u32 *)ramv_buf1)[i]);
			}
		}
		if(err == 0)
			printk("%s: ramv buf validation ok!!! line:%d index %d\n", __func__, __LINE__, i);

		err =0;
		for(i =0; i < (RAMC_BUF2_SZ/4); i++) {
			if(((u32 *)ramv_buf2)[i] == 0x55555555) {
				continue;
			}
			else {
				err++;
				printk("%s:%d: ramv buf2 validation fails!!! index %d err: %d\n", __func__, __LINE__, i, err);
			}
		}
		if(err == 0)
			printk("%s: ramv buf2 validation ok!!! line:%d index %d\n", __func__, __LINE__, i);

		printk(KERN_DEBUG "compare complete1   -----------\n");
	}
#endif

	writel(0, sarram + SRAM_RESUME_ENTRY);
	writel(~0, sarram + SRAM_SYS_STAT);

	outer_resume();
	dsb();

	WakeupTimerClrClk32K(test_id);
	stop_debug_timer();
	exit_s3 = 1;

#ifdef CONFIG_ARM_ARCH_TIMER
	/*
	 * This configuration is NOT necessary, cause how to calculate
	 * time elapse is determined by
	 * 1. arch_timer_rate, which is a virtual variable initialized at boot stage;
	 * 2. and cycles, which depends on physical freq;
	 * these 2 factors are both physically present and valid.
	 * Here we still configurate freq in order to avoid misapprehension
	 */
    asm volatile ("mcr p15, 0, %0, c14, c0, 0" : : "r"(syscounter_freq));
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (syscounter_freq));
	printk(KERN_DEBUG "syscounter freq %dHz\n", syscounter_freq);
#endif

	local_irq_restore(flags);

	if(memfree) {
		if(ramv_buf1)
			kfree(ramv_buf1);
		if(ramv_buf2)
			kfree(ramv_buf2);
		if(ramv_buf3)
			kfree(ramv_buf3);
		ramv_buf1 = ramv_buf2 = ramv_buf3 = NULL;
	}

	if(wa) {
		printk(KERN_DEBUG "wa %s:%s:%d\n", __FILE__, __func__, __LINE__);
#ifdef CLK_LIM_OLD_WA
#ifdef CAPSL_S
		nufront_prcm_write(v[0], 0x80);
		nufront_prcm_write(v[1], 0x184);
		nufront_prcm_write(v[2], 0x200);
		nufront_prcm_write(v[3], 0x204);
#endif
		nufront_prcm_write(v[4], 0x180);
#else
		clk_limitation_workaround(true);
#endif
	}
	dump_priv_regs(get_prcm_base(), 140, "prcm1", 0);

	show_wakeup_events(escape);
	return 0;
}

/*
 *	1. IC DIST I/F is enabled;
 *	2. IC CPU I/F is enabled;
 *	3. CPU IRQ is enabled;
 *	4. NO_SUSPEND IRQs are touchable;
 *	5. non-NO_SUSPEND IRQs are still untouchable;
 */
static void __maybe_unused n7s3_wake(void)
{

}

static struct platform_suspend_ops n7s3_pm_ops = {
	.valid		= suspend_valid_only_mem,
	.enter		= n7s3_enter,
	.begin		= n7s3_begin,
#if 0
	.wake		= n7s3_wake,
	.finish		= n7s3_finish,
	.end		= n7s3_end,
#endif
};

static int __init n7s3_pm_init(void)
{
	if(suspend_ctrl & 0x80) {
		// Only for debug using that checks memory consistence issues when suspend_ctrl is switched value of 0x80
		printk(KERN_DEBUG "%s:%s:%d PAGE_SIZE %ld\n", __FILE__, __func__, __LINE__, PAGE_SIZE);
		ramv_buf1 = kzalloc(RAMC_BUF_SZ, GFP_KERNEL);
		ramv_buf2 = kzalloc(RAMC_BUF2_SZ, GFP_KERNEL);
		ramv_buf3 = kzalloc(RAMC_BUF3_SZ, GFP_KERNEL);

		if(!ramv_buf1 || !ramv_buf2)
			return -ENOMEM;
	}

	indicator.phys_off = PHYS_OFFSET;
	indicator.page_off = PAGE_OFFSET;

	n7s3_set_mode(0, CTRL_S3_INIT);
	suspend_set_ops(&n7s3_pm_ops);

	// enable wakeup timer if you required
	enable_wktimer(true);
	return 0;
}

late_initcall(n7s3_pm_init);
#endif	/* CONFIG_SUSPEND */
