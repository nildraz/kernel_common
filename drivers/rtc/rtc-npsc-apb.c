
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#define DRV_VERSION "0.1"

#define SUPPORT_WAKEUP 1

#define NPSC_RTC_CCVR   0x00
#define NPSC_RTC_CMR    0x04
#define NPSC_RTC_CLR    0x08
#define NPSC_RTC_CCR    0x0c
#define NPSC_RTC_STAT   0x10
#define NPSC_RTC_RSTAT  0x14
#define NPSC_RTC_EOI    0x18
#define NPSC_RTC_COMP_VERSION  0x1c

#define NPSC_RTC_CCR_WEN     BIT(3)
#define NPSC_RTC_CCR_EN      BIT(2)
#define NPSC_RTC_CCR_MASK    BIT(1)
#define NPSC_RTC_CCR_IEN     BIT(0)
#define NPSC_RTC_STAT_INT    BIT(0)


static int npsc_rtc_alarm_irq;
static void __iomem *npsc_rtc_base;

static void prcm_isolate_hardware(int enable);

#define FIFO_SIZE 8

static u32 npsc_rtc_read(u32 reg ){
	int i,value;
	value = __raw_readl(npsc_rtc_base+reg);
	for(i=0;i<FIFO_SIZE-1;i++)
		__raw_readl(npsc_rtc_base+0x3c);
	return value;
}

static void npsc_rtc_write(u32 value, u32 reg){
	int i;
	__raw_writel(value, npsc_rtc_base+reg);
	for(i=0;i<FIFO_SIZE-1;i++)
		__raw_writel(0,npsc_rtc_base+0x3c);
}

#define prcm_rtc_online(x) do{ spin_lock_irqsave(&prcm_lock,flag); prcm_isolate_hardware(0);}while(0)
#define prcm_rtc_offline(x) do{prcm_isolate_hardware(1); spin_unlock_irqrestore(&prcm_lock,flag);}while(0)

#define prcm_rtc_irq_online(x) do{ spin_lock(&prcm_lock); prcm_isolate_hardware(0);}while(0)
#define prcm_rtc_irq_offline(x) do{prcm_isolate_hardware(1); spin_unlock(&prcm_lock);}while(0)

static int npsc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled);

/*
Register PRCM_RTC_MISC_CTRL 0x5200000+0x022c

bits 2 rtc_mask        enable:1 disable:0 default:1
bits 1 rtc_rstn        enable:0 disable:1 default:1
bits 0 rtc_isolate_n   enable:0 disable:1 default:0
*/

#define PRCM_RTC_MISC_CTRL 	 (0x5200000+0x022c)
#define PRCM_RTC_MASK     BIT(2)
#define PRCM_RTC_RSTN     BIT(1)
#define PRCM_RTC_ISOLATE  BIT(0)

static void __iomem *prcm;
static spinlock_t prcm_lock;
static unsigned long flag;

static int prcm_init(void)
{
	prcm = ioremap(PRCM_RTC_MISC_CTRL, 4);
	if(!prcm) {
		pr_err();
		return -ENOMEM;
	}

	spin_lock_init(&prcm_lock);
	return 0;
}

static void prcm_releas(void)
{
	if(prcm) {
		iounmap(prcm);
		prcm = NULL;
	}
}

static void prcm_active_hardware(void)
{
	void __iomem *io = prcm;
	u32 data = 0;

	pr_debug("PRCM_RTC_MISC_CTRL : 0x%x\n", readl(io));

	/* unmask RTC. */
	data = readl(io);
	data &= ~(PRCM_RTC_MASK);
	writel(data, io);
	udelay(1);

	prcm_rtc_online();
	if(npsc_rtc_read(NPSC_RTC_CCVR) >0) {
		pr_debug("npsc_rtc already been actived.\n");
		prcm_rtc_offline();
		return;
	}
	pr_debug("active npsc_rtc.\n");

	/* factory reset. */

	/* reset */
	writel(PRCM_RTC_ISOLATE, io);//rtc_mask disable | rtc_rstn enable | rtc_isolate disable
	udelay(1);
	/* releass reset */
	writel(PRCM_RTC_RSTN|PRCM_RTC_ISOLATE, io);//rtc_rstn disable | rtc_isolate disable

	/* start counter */
	npsc_rtc_write(0, NPSC_RTC_CLR);
	npsc_rtc_write(NPSC_RTC_CCR_EN, NPSC_RTC_CCR);

	writel(PRCM_RTC_RSTN, io);//rtc_rstn disable | rtc_isolate enable
	prcm_rtc_offline();
	return;
}


static void prcm_isolate_hardware(int enable)
{
	void __iomem *io = prcm;
	u32 data = 0;

	data = readl(io);

	if(enable){
		data &= ~PRCM_RTC_ISOLATE;
	}
	else{
		data |= PRCM_RTC_ISOLATE;
	}

	writel(data, io);
}

static int npsc_get_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long ticks = 100;

	prcm_rtc_online();
	ticks = npsc_rtc_read(NPSC_RTC_CCVR);
	prcm_rtc_offline();

	rtc_time_to_tm(ticks, tm);
	return rtc_valid_tm(tm);
}

static int npsc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned long ticks;

	rtc_tm_to_time(tm, &ticks);

	prcm_rtc_online();
	npsc_rtc_write(ticks, NPSC_RTC_CLR);
	prcm_rtc_offline();

	return 0;
}

static int npsc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned long ticks;

	/* convert alarm time to seconed to setup NPSC_RTC_CMR. */
	rtc_tm_to_time(&alrm->time, &ticks);

	prcm_rtc_online();
	npsc_rtc_write(ticks, NPSC_RTC_CMR);
	prcm_rtc_offline();

	npsc_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static int npsc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned long ticks;

	/* read NPSC_RTC_CMR to time */
	prcm_rtc_online();
	ticks = npsc_rtc_read(NPSC_RTC_CMR);

	rtc_time_to_tm(ticks, &alrm->time);

	/* is int enabled? */
	alrm->enabled = (npsc_rtc_read(NPSC_RTC_CCR) & NPSC_RTC_CCR_IEN)?1:0;
	prcm_rtc_offline();

	return 0;
}

static int npsc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	u32 data = 0;
	prcm_rtc_online();
	data = npsc_rtc_read(NPSC_RTC_CCR);
	if(enabled)
		data |= NPSC_RTC_CCR_IEN;
	else
		data &= ~NPSC_RTC_CCR_IEN;

	npsc_rtc_write(data, NPSC_RTC_CCR);
	prcm_rtc_offline();
	return 0;
}

static irqreturn_t npsc_rtc_alarmirq(int irq, void *data)
{
	struct rtc_device *rtc = (struct rtc_device *)data;

	prcm_rtc_irq_online();
	if(npsc_rtc_read(NPSC_RTC_STAT) & NPSC_RTC_STAT_INT) {
		npsc_rtc_read(NPSC_RTC_EOI);
		rtc_update_irq(rtc, 1, RTC_AF);
	}

	prcm_rtc_irq_offline();

	return IRQ_HANDLED;
}

static const struct rtc_class_ops npsc_rtc_ops = {
	.read_time = npsc_get_time,
	.set_time = npsc_set_time,
	.set_alarm = npsc_rtc_set_alarm,
	.read_alarm = npsc_rtc_read_alarm,
	.alarm_irq_enable = npsc_rtc_alarm_irq_enable,
};

static int npsc_rtc_suspend(struct device *dev)
{
#if 0//SUPPORT_WAKEUP
	if(device_may_wakeup(dev)) {
		enable_irq_wake(npsc_rtc_alarm_irq);
	}
#endif
	return 0;
}

static int npsc_rtc_resume(struct device *dev)
{
#if 0//SUPPORT_WAKEUP
	if(device_may_wakeup(dev)) {
		disable_irq_wake(npsc_rtc_alarm_irq);
	}
#endif
	return 0;
}

static SIMPLE_DEV_PM_OPS(npsc_rtc_pm_ops, npsc_rtc_suspend, npsc_rtc_resume);

static const struct of_device_id npsc_rtc_of_match[] = {
	{ .compatible = "npsc,npsc-apb-rtc", },
	{},
};

static int  npsc_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct resource *res;
	int ret = 0;

	/* get the io memory */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -EINVAL;
	}

	npsc_rtc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(npsc_rtc_base))
		return PTR_ERR(npsc_rtc_base);

	/* set alarm irq */
	npsc_rtc_alarm_irq = platform_get_irq(pdev, 0);
	if (npsc_rtc_alarm_irq < 0) {
		dev_err(&pdev->dev, "no irq for alarm\n");
		goto err_iomap;
	}

	ret = prcm_init();
	if(ret) {
		goto err_irq;
	}
	prcm_active_hardware();

	prcm_rtc_online();
	pr_info("RTC hardware version: %x\n", npsc_rtc_read(NPSC_RTC_COMP_VERSION));
	prcm_rtc_offline();

#if SUPPORT_WAKEUP
	device_init_wakeup(&pdev->dev, 1);
#endif

	/* register RTC device */
	rtc = devm_rtc_device_register(&pdev->dev, "rtc-nufront",
					&npsc_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(rtc);
		goto err_irq;
	}

	ret = devm_request_irq(&pdev->dev, npsc_rtc_alarm_irq, npsc_rtc_alarmirq,
			  0,  "npsc-apb-rtc alarm", rtc);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", npsc_rtc_alarm_irq, ret);
		goto err_dev;
	}

	rtc->uie_unsupported = 1;
	platform_set_drvdata(pdev, rtc);
	pr_debug("RTC device registered.\n");
	return 0;

err_dev:
	devm_rtc_device_unregister(&pdev->dev, rtc);
err_irq:
	npsc_rtc_alarm_irq = -1;
err_iomap:
	iounmap(npsc_rtc_base);
	npsc_rtc_base = NULL;
	return ret;
}

static int npsc_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = (struct rtc_device *)platform_get_drvdata(pdev);

	if(npsc_rtc_alarm_irq >0)
		free_irq(npsc_rtc_alarm_irq, rtc);

	devm_rtc_device_unregister(&pdev->dev, rtc);

	if(npsc_rtc_base)
		iounmap(npsc_rtc_base);

	prcm_releas();
	return 0;
}

static struct platform_driver npsc_rtc_driver = {
	.probe  = npsc_rtc_probe,
	.remove = __exit_p(npsc_rtc_remove),
	.driver = {
		.name = "npsc-apb-rtc",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM_SLEEP
		.pm = &npsc_rtc_pm_ops,
#endif
		.of_match_table	= of_match_ptr(npsc_rtc_of_match),
	},
};

module_platform_driver(npsc_rtc_driver);

MODULE_AUTHOR("songyouyu <youyu.song@nufront.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("npsc RTC driver");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:rtc-npsc");
