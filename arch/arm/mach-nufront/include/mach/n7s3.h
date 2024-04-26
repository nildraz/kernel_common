#ifndef __N7S3_H__
#define __N7S3_H__

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/list.h>
#endif

#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/crc16.h>
#include <linux/timer.h>


#define NPSC01_DRAM_RESERVED_BASE	0x8fffc000   /* reserved ddr region 12K */
#define NPSC01_IRAM_BASE			0x07000000   /* IRAM 64K*/


#define SCU_BASE					0x05040000
#define L220_BASE					0x05042000

#define OFFSET_GIC_CPU_IF			0x100
#define OFFSET_GIC_DIST_IF			0x1000

#define TIMER1_BASE					0x05220028   /*using timer 2*/
#define SARRAM_BASE					0x05800000
#define SRAM_DFS_MHZ				0x05800FE4
#define SRAM_BACK_ENTRY				0x05800FE0

/* S3 mode definition */
#define MODE_12M_CLK				(0x1<<0)
#define MODE_RESERVE_DDR			(0x1<<1)
#define MODE_BUS_CLK_STOP			(0x1<<2)
#define MODE_POLL_ASR				(0x1<<3)        /* new */

#define MODE_WAKE_UP				(0x1<<4)
#define MODE_DDR_RETENTION			(0x1<<5)        /* re-defined */
#define MODE_DDR_SR					(0x1<<6)
#define MODE_S3_REPEAT				(0x1<<7)

//#define MODE_AP2BP_ISO                (0x1<<8)        /* not supported, set when using BP JTAG */
//#define MODE_DDR_NONE           (0x1<<8)
#define MODE_INTERNAL_SUS			(0x1<<9)        /* Do generate sus signal */
#define MODE_DDR_ASR				(0x1<<10)	/* new */
#define MODE_CLK_WORKAROUND			(0x1<<11)	/* new */

#define MODE_DQ_DQS_RX_EN_4G		(0x1<<12)       /* DDR DQ & DQS 4 groups ctrl */
#define MODE_FEEDBACK_CTRL			(0x1<<13)       /* DDR feedback control */

#define MODE_S3_TEST				(0x1<<28)
#define MODE_AP_NORMAL				(0x1<<29)       /* auto-self-refresh */
#define MODE_BP_FLIGHT				(0x1<<30)       /* m_retention */
#define MODE_CLKSTOP				(0x1<<31)       /* AP clock stop */

/* Leave BP flight alone */
#define S3_MODE_MASK				(0x7 << 29)
#define CTRL_S3_INIT				(0x1<<0)
#define CTRL_S3_SET					(0x1<<1)
#define CTRL_S3_CLEAR				(0x1<<2)

/* SRAM definition */
#define SRAM_RESUME_ENTRY			0x15FC
#define SRAM_SYS_STAT				0x15F8
#define SRAM_MISC					0x15F4
#define SRAM_FW_STAT				0x15F0
#define SRAM_S3_MODE				0x15EC
#define SRAM_WKUP_INTVL				0x15E8
#define SRAM_DDR_MEMCMP				0x1600
#define SRAM_DDR_MEMCMP2				0x1604
#define SRAM_DDR_MEMCMP3				0x1608

/* wakeup events */
#define WAKEUP_IRQ_CTRL1			0x50
#define WAKEUP_IRQ_STATUS			0x3c
#define WAKRUP_TIMER_CLK_CTRL       0x01B4

#define WKEV_BP						(1 << 16)
#define WKEV_WKGPIO					(1 << 17)
#define WKEV_WKTIMER				(1 << 18)
#define WKEV_COREGPIO				(1 << 19)

struct priv_resume {
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct list_head link;
	int level;
	void (*resume) (struct priv_resume *pr);
#endif
};

enum {
	PRIV_RESUME_LEVEL_0	= 0,	/* 1 ~ 9 leave to more detailed ops */
	PRIV_RESUME_LEVEL_1	= 10,
	PRIV_RESUME_LEVEL_2	= 20,
	PRIV_RESUME_LEVEL_3	= 30,
	PRIV_RESUME_LEVEL_4	= 40,
};


#ifdef CONFIG_HAS_EARLYSUSPEND
void register_priv_resume(struct priv_resume *handler);
void unregister_priv_resume(struct priv_resume *handler);
void priv_resume(void);
#else
#define register_priv_resume(handler)  do {} while(0)
#define unregister_priv_resume(handler)  do {} while(0)
#define priv_resume()			do {} while(0)
#endif

struct freq_table {
	unsigned int index;
	unsigned int freq;
};

enum freq_id {
	DDR_FREQ_533M = 0,
	DDR_FREQ_400M,
	DDR_FREQ_333M,
	DDR_FREQ_200M,
	DDR_FREQ_END,
};

struct ddr_config {
	unsigned int offset;
	unsigned int value;
};

struct ehr_struct {
	struct hrtimer timer;
	//struct early_suspend early_suspend;
	int state;
	int interval;
	int es_complete;
	int lr_complete;
	unsigned long pass;
	unsigned long lr_fail;
	unsigned long es_fail;
};

enum cfs_types {
	CFS_TYPE_CPU0_2_CPU0 = 0,
	CFS_TYPE_CPU0_2_CPU1,
	CFS_TYPE_CPU0_2_CPU2,
	CFS_TYPE_CPU0_2_CPU3,

	CFS_TYPE_CPU1_2_CPU0,
	CFS_TYPE_CPU1_2_CPU1,
	CFS_TYPE_CPU1_2_CPU2,
	CFS_TYPE_CPU1_2_CPU3,

	CFS_TYPE_CPU2_2_CPU0,
	CFS_TYPE_CPU2_2_CPU1,
	CFS_TYPE_CPU2_2_CPU2,
	CFS_TYPE_CPU2_2_CPU3,

	CFS_TYPE_CPU3_2_CPU0,
	CFS_TYPE_CPU3_2_CPU1,
	CFS_TYPE_CPU3_2_CPU2,
	CFS_TYPE_CPU3_2_CPU3,
	NR_SCF_TYPES,
};

struct debug_timer {
	int irq;
	int ms;
	int cpu;
	unsigned int requested;
	struct clk *clk;
	void __iomem *va;
	spinlock_t lock;
	struct tasklet_struct tasklet;
	struct workqueue_struct *wq;
	struct work_struct work;
	struct call_single_data call_func_data;
	struct timer_list swt;
	struct debug_stats {
		/* wq/tl stats */
		unsigned long test_cnt;
		unsigned long irq_recv;
		/* wq stats */
		unsigned long work_pinned_cpu[NR_CPUS];
		unsigned long queue_work_fail[NR_CPUS];
		unsigned long wield_cpu[NR_CPUS];

		/* smp call func stats */
		unsigned int sender;
		unsigned int recver;
		unsigned long cfs_send[NR_SCF_TYPES];
		unsigned long cfs_handled[NR_SCF_TYPES];
		enum cfs_types cfs_type;
		unsigned short crc16;
		char *read_buf;
		char *write_buf;

		/* swt stats */
		unsigned long swt_handled;
		unsigned long swt_miss;
		atomic_long_t swt_touch;
	} stats;
	unsigned int buf_sz;
	char *src_buf;
	char *dest_buf;
	struct ehr_struct *ehr;
};

void ddr_save(void);
int ddr_switch_freq(unsigned int freq_mhz);
int is_clkstop_mode(void);
#endif	/* __N7S3_H__ */
