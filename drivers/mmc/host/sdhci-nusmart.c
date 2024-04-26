#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/of.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include "sdhci-nusmart.h"
#include "sdhci.h"
#include "sdhci-pltfm.h"

#define  SD_MAX_CLK   96000000


struct mmc_host *sdio_host;
/*
struct sdhci_nufront_soc_data {
	struct sdhci_pltfm_data *pdata;
	u32 nvquirks;
};

struct sdhci_nufront {
	const struct nusmart_sdhci_platform_data *plat;
	const struct sdhci_nufront_soc_data *soc_data;
};
*/
#ifdef CONFIG_PM_RUNTIME
static void sdhci_nusmart_set_host_clock(struct nusmart_sdhci_platform_data *plat,int enable);
#else
static inline void sdhci_nusmart_set_host_clock(struct nusmart_sdhci_platform_data *plat,int enable)
{
	return;
}
#endif
static u32 nusmart_sdhci_readl(struct sdhci_host *host, int reg)
{
	u32 val;
	if (!(host->quirks & SDHCI_QUIRK_INVERTED_WRITE_PROTECT)) {
		if (unlikely(reg == SDHCI_PRESENT_STATE)) {
			/* Use wp_gpio here instead? */
			val = readl(host->ioaddr + reg);
			return val | SDHCI_WRITE_PROTECT;
		}
	}
	return readl(host->ioaddr + reg);
}

static u16 nusmart_sdhci_readw(struct sdhci_host *host, int reg)
{
	if (unlikely(reg == SDHCI_HOST_VERSION)) {
		/* Erratum: Version register is invalid in HW. */
		return SDHCI_SPEC_300;
	}

	return readw(host->ioaddr + reg);
}

static void nusmart_sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	/* Seems like we're getting spurious timeout and crc errors, so
	 * disable signalling of them. In case of real errors software
	 * timers should take care of eventually detecting them.
	 */
	if (unlikely(reg == SDHCI_SIGNAL_ENABLE))
		val &= ~(SDHCI_INT_TIMEOUT|SDHCI_INT_CRC);

	writel(val, host->ioaddr + reg);

	if (unlikely(reg == SDHCI_INT_ENABLE)) {
		/* Erratum: Must enable block gap interrupt detection */
		u8 gap_ctrl = readb(host->ioaddr + SDHCI_BLOCK_GAP_CONTROL);
		if (val & SDHCI_INT_CARD_INT)
			gap_ctrl |= 0x8;
		else
			gap_ctrl &= ~0x8;
		writeb(gap_ctrl, host->ioaddr + SDHCI_BLOCK_GAP_CONTROL);
	}
}



static int nusmart_sdhci_bus_width(struct sdhci_host *host, int bus_width)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));

	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;

	u32 ctrl;

	u32 val = readl(plat->addr);


	ctrl = sdhci_readb(host, SDHCI_HOST_CONTROL);

	if (plat->is_8bit && bus_width == MMC_BUS_WIDTH_8) {
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		ctrl |= SDHCI_CTRL_8BITBUS;
		val &= ~(0x1000000<<(pdev->id));
		writel(val, plat->addr);
	} else {
		ctrl &= ~SDHCI_CTRL_8BITBUS;
		if (bus_width == MMC_BUS_WIDTH_4) {
			ctrl |= SDHCI_CTRL_4BITBUS;
			val &= ~(0x1000000<<(pdev->id));
			writel(val, plat->addr);
		} else
			ctrl &= ~SDHCI_CTRL_4BITBUS;
	}
	sdhci_writeb(host, ctrl, SDHCI_HOST_CONTROL);

	return 0;
}

static void sd_host_init(struct nusmart_sdhci_platform_data *plat)
{
	unsigned int count = 0;
	u32 val;
	plat->addr = ioremap(plat->mmc_base, SZ_1K);
	//if((plat->scm_offset > 0) && (plat->drv_strength != nufront_scm_readl(plat->scm_offset)))
	//	nufront_scm_writel(plat->drv_strength,plat->scm_offset);

	//if((plat->scm_offset2 > 0) && (plat->drv_strength2 != nufront_scm_readl(plat->scm_offset2)))
	//	nufront_scm_writel(plat->drv_strength2,plat->scm_offset2);
	val = readl(plat->addr);
	writel(val|0x1, plat->addr);
	while (readl(plat->addr) & 0x1) {
		udelay(5);
		if (count++ > 100) {
			printk(KERN_ERR"Rest control failed\n");
			break;
		}
	}
	/*Debounce Period*/
	writel(0x300000ul, plat->addr+0x04);

	printk(KERN_INFO "Exit %s\n", __FUNCTION__);

}

static int sdhci_nufront_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
#ifndef CONFIG_PM_RUNTIME
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
#endif
	int dead = (readl(host->ioaddr + SDHCI_INT_STATUS) == 0xffffffff);
	sdhci_remove_host(host, dead);
	pm_runtime_enable(host->mmc->parent);

#ifndef CONFIG_PM_RUNTIME
	if(!IS_ERR(plat->sd_clk))
		clk_disable_unprepare(plat->sd_clk);
#endif
	sdhci_pltfm_free(pdev);
	platform_set_drvdata(pdev,NULL);
	return 0;
}

static unsigned int sdhci_get_max_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
	return plat->freq;
}

static unsigned int sdhci_get_min_clk(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	const struct nusmart_sdhci_platform_data *plat = nufront_host->plat;
	return plat->f_min;
}

static void    sdhci_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u32 tmp;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
	unsigned int div = 0;
	int source_rate;
	if (host->clock == clock || (clock == 0 && plat->ctype == SDIO_CARD))
		return;

	if (plat->ctype == EMMC_CARD)
		sdhci_writel(host, 0x10, 0x70);
	if(plat->ctype == SD_CARD)
		sdhci_writel(host, 0x30, 0x70);
	host->clock = clock;

	source_rate = plat->ref_clk;
	if (clock != 0) {
		if (clock >= source_rate)
			div = 0;
		else {
			for (div = 1; div < 256; div++) {
				if (source_rate / (2 * div) <= clock)
					break;
			}
		}
	}
	else
		div = 0;
	if (div >= 0xFF)
		printk(KERN_ERR"clock divider value error\n");
	tmp = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
	tmp = tmp & (~0x0000004ul);
	sdhci_writel(host, tmp, SDHCI_CLOCK_CONTROL);

	if(clock != 0) {
		tmp = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
		tmp = tmp & (~0x000FF00ul);
		sdhci_writel(host, tmp, SDHCI_CLOCK_CONTROL);
		sdhci_writel(host, (div<<8)|tmp|0x0000001ul, SDHCI_CLOCK_CONTROL);
		while ((0x2&sdhci_readl(host, SDHCI_CLOCK_CONTROL)) != 2)
			udelay(100);
		tmp = sdhci_readl(host, SDHCI_CLOCK_CONTROL);
		sdhci_writel(host, tmp|0x0000004ul, SDHCI_CLOCK_CONTROL);
	}
}

void sdhci_sdio_reg_restore(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;

	unsigned long flags;
	unsigned int  val, cnt = 0;

	if (plat->ctype == SDIO_CARD) {
		spin_lock_irqsave(&host->lock, flags);
		/*Enable the slot power for SDIO*/

		val = sdhci_readl(host, SDHCI_HOST_CONTROL);
		val &= ~(0x0000E00ul | 0x0000100ul);
		val |= 0x0000E00ul | 0x0000100ul;
		sdhci_writel(host, val, SDHCI_HOST_CONTROL);

		/*Set the data bus width*/
		val = readl(plat->addr);
		val &= ~(0x1000000<<1);
		writel(val, plat->addr);

		val = sdhci_readl(host, SDHCI_HOST_CONTROL);
		val |= 0x12;
		sdhci_writel(host, val, SDHCI_HOST_CONTROL);
		/*set clk*/
		val = 0x0000001ul | (0x2<<8);
		sdhci_writel(host, val, SDHCI_CLOCK_CONTROL);
		while ((0x02 & sdhci_readl(host, SDHCI_CLOCK_CONTROL)) != 0x2) {
			udelay(2);
			cnt++;
			if (cnt > 100) {
				printk(KERN_ERR"waiting for Internal Clock Stable timeout");
				break;
			}
		}
		val |= 0x0000001ul;
		sdhci_writel(host, val, SDHCI_CLOCK_CONTROL);

		spin_unlock_irqrestore(&host->lock, flags);
	}
}

unsigned long int  nusmart_sdhci_get_pdata_from_of(const struct device_node *np, char *node)
{
	u32 val;
	if (of_property_read_u32(np, node, &val)) {
		printk(KERN_ERR"can not find %s in DTS\n", node);
		return -EINVAL;
	}

	return val;
}

void nusmart_sdhci_get_of_property(struct platform_device *pdev,
		struct nusmart_sdhci_platform_data *plat,
		const struct device_node *np)
{
	plat->ref_clk = nusmart_sdhci_get_pdata_from_of(np, "ref_clk");
	plat->freq = nusmart_sdhci_get_pdata_from_of(np, "freq");
	plat->f_min = nusmart_sdhci_get_pdata_from_of(np, "f_min");
	plat->ctype = nusmart_sdhci_get_pdata_from_of(np, "ctype");
	plat->is_8bit = nusmart_sdhci_get_pdata_from_of(np, "is_8bit");
	plat->no_wp = nusmart_sdhci_get_pdata_from_of(np, "no_wp");
	plat->no_dp = nusmart_sdhci_get_pdata_from_of(np, "no_dp");
	plat->pm_caps = nusmart_sdhci_get_pdata_from_of(np, "pm_caps");
	plat->caps = nusmart_sdhci_get_pdata_from_of(np, "caps");
	plat->ocr_avail = nusmart_sdhci_get_pdata_from_of(np, "ocr_avail");
	plat->mmc_base = nusmart_sdhci_get_pdata_from_of(np, "mmc_base");
	plat->drv_strength = nusmart_sdhci_get_pdata_from_of(np,"drv_strength");
	plat->drv_strength2 = nusmart_sdhci_get_pdata_from_of(np,"drv_strength2");
	plat->scm_offset = nusmart_sdhci_get_pdata_from_of(np,"scm_offset");
	plat->scm_offset2 = nusmart_sdhci_get_pdata_from_of(np,"scm_offset2");
}

static struct sdhci_ops nusmart_sdhci_ops = {
	.get_ro     = NULL,
	.read_l     = nusmart_sdhci_readl,
	.read_w     = nusmart_sdhci_readw,
	.write_l    = nusmart_sdhci_writel,
	.platform_bus_width = nusmart_sdhci_bus_width,
	.get_max_clock 	= sdhci_get_max_clk,
	.get_min_clock  = sdhci_get_min_clk,
	.set_clock      = sdhci_set_clock,
	.platform_resume = NULL,/*sdhci_sdio_reg_restore,*/
};

/*
 * SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12: ACMD12 buggy, TO waiting for HW interrupt;
 * SDHCI_QUIRK_NO_HISPD_BIT: host HS & DS OK;
 */

#define	COMMON_QUIRKS	(/*SDHCI_QUIRK_NO_CARD_NO_RESET |*/ 			\
				/* enable host default speed */			\
				/* SDHCI_QUIRK_NO_HISPD_BIT |*/			\
				/* we config ref clock */			\
				SDHCI_QUIRK_NONSTANDARD_CLOCK|			\
				SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN | 		\
				SDHCI_QUIRK_MISSING_CAPS |			\
				/*SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12| */	\
				SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC|		\
				SDHCI_QUIRK_32BIT_ADMA_SIZE|			\
				/*SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12 |*/	\
				SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC)

struct sdhci_pltfm_data sdhci_nusmart_slot0 = {
	.quirks = COMMON_QUIRKS | /*SDHCI_QUIRK_FORCE_1_BIT_DATA |*/
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12|
		  SDHCI_QUIRK_SINGLE_POWER_WRITE,
	.ops  = &nusmart_sdhci_ops,
};

struct sdhci_pltfm_data sdhci_nusmart_slot1 = {
	.quirks = COMMON_QUIRKS |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12|
		  SDHCI_QUIRK_SINGLE_POWER_WRITE,
	.ops  = &nusmart_sdhci_ops,
};

struct sdhci_pltfm_data sdhci_nusmart_slot2 = {
	.quirks = COMMON_QUIRKS |
		  SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		  SDHCI_QUIRK_BROKEN_TIMEOUT_VAL |
		  SDHCI_QUIRK2_HOST_OFF_CARD_ON |
		  SDHCI_QUIRK_SINGLE_POWER_WRITE,
	.ops  = &nusmart_sdhci_ops,
};

struct sdhci_nufront_soc_data sdhci_nusmart_sd = {
	.pdata = &sdhci_nusmart_slot0,
};

struct sdhci_nufront_soc_data sdhci_nusmart_emmc = {
	.pdata = &sdhci_nusmart_slot1,
};

struct sdhci_nufront_soc_data sdhci_nusmart_sdio = {
	.pdata = &sdhci_nusmart_slot2,
};


static int sdhci_nufront_probe(struct platform_device *pdev)
{
	const struct sdhci_nufront_soc_data *soc_data;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct nusmart_sdhci_platform_data *plat;
	struct sdhci_nufront *nufront_host;
	int rc = 0;
	u32 val;
	unsigned int mmc_base;
	const struct device_node *np = pdev->dev.of_node;
	if (np == NULL) {
		printk(KERN_ERR"find of device node is NULL\n");
		return -1;
	}
	if (of_property_read_u32(np, "id", &val)) {
		printk(KERN_ERR" can not get id\n");
		return -EINVAL;
	}

	pdev->id = val;

	printk(KERN_DEBUG"Entern the %s pdev->id = %d\n", __func__, pdev->id);
	if (pdev->id == 0)
		soc_data = &sdhci_nusmart_sd;
	if (pdev->id == 1)
		soc_data = &sdhci_nusmart_emmc;
	if (pdev->id == 2)
		soc_data = &sdhci_nusmart_sdio;
	pdev->dev.id = pdev->id;
	host = sdhci_pltfm_init(pdev, soc_data->pdata);
	if (IS_ERR(host))
		return PTR_ERR(host);
	pltfm_host = sdhci_priv(host);
	plat = devm_kzalloc(&pdev->dev, sizeof(*plat), GFP_KERNEL);
	if (plat == NULL) {
		dev_err(mmc_dev(host->mmc), "missing platform data\n");
		rc = -ENXIO;
		goto err_no_plat;
	}

	nusmart_sdhci_get_of_property(pdev, plat, np);


	mmc_base = plat->mmc_base;
	/*this functio is want to power on wifi which it can be scan sdio card befor
	 *the wifi probe.
	 * */
       if(plat->ctype == SDIO_CARD) {
               plat->wifi_init = bcmdhd_wifi_init;
               if (plat->wifi_init)
                       plat->wifi_init();
       }

	nufront_host = devm_kzalloc(&pdev->dev, sizeof(*nufront_host), GFP_KERNEL);
	if (!nufront_host) {
		dev_err(mmc_dev(host->mmc), "failed to allocate tegra_host\n");
		rc = -ENOMEM;
		goto err_no_plat;
	}
	nufront_host->plat = plat;
	nufront_host->soc_data = soc_data;

	pltfm_host->priv = nufront_host;
	plat->sd_clk = devm_clk_get(&pdev->dev, "sd_clk");
	if (!IS_ERR(plat->sd_clk)) {
		clk_set_rate(plat->sd_clk, SD_MAX_CLK);
		clk_prepare_enable(plat->sd_clk);
	}
	sd_host_init(plat);

	pltfm_host->clk = plat->sd_clk;

	host->mmc->pm_caps = plat->pm_caps;
	if (plat->is_8bit)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	host->caps = plat->caps;
	if (plat->ctype == SD_CARD)
		host->ocr_avail_sd = plat->ocr_avail;

	if (plat->ctype == EMMC_CARD) {
		host->ocr_avail_mmc = plat->ocr_avail;
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
	}

	if (plat->ctype == SDIO_CARD) {
		host->ocr_avail_sdio = plat->ocr_avail;
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
		host->quirks2 |= SDHCI_QUIRK2_HOST_OFF_CARD_ON;
	}
	if (plat->no_wp)
		host->quirks |= SDHCI_QUIRK_INVERTED_WRITE_PROTECT;
	if (plat->no_dp){
		host->quirks |= SDHCI_QUIRK_BROKEN_CARD_DETECTION;
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
	}

	if(plat->ctype != SDIO_CARD){
		if(plat->ctype == EMMC_CARD)
			pm_runtime_enable(host->mmc->parent);
		else if((plat->ctype == SD_CARD) && plat->no_dp)
			pm_runtime_enable(host->mmc->parent);
	}
	rc = sdhci_add_host(host);
	if (rc)
		goto err_add_host;

	if (plat->ctype == SDIO_CARD)
		sdio_host = host->mmc;

	sdhci_nusmart_set_host_clock(plat,0);

	return 0;
err_add_host:
	clk_disable_unprepare(plat->sd_clk);

err_no_plat:
	sdhci_pltfm_free(pdev);
	return rc;
}
EXPORT_SYMBOL_GPL(sdio_host);


#ifdef CONFIG_PM
static int sdhci_nusmart_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct nusmart_sdhci_platform_data *plat = NULL;
	int ret;
#ifdef CONFIG_PM_RUNTIME
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
#endif
	/*
	 *Because resume host will write/read sdhci host register,
	 *so we must enable the bus clk and sd clk
	 */
	sdhci_nusmart_set_host_clock(plat,1);
	ret =  sdhci_suspend_host(host);
	sdhci_nusmart_set_host_clock(plat,0);
	return ret;
}

static int sdhci_nusmart_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct nusmart_sdhci_platform_data *plat;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	int ret;

	plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;

	if (!IS_ERR(plat->sd_clk)) {
		clk_set_rate(plat->sd_clk, SD_MAX_CLK);
		clk_prepare_enable(plat->sd_clk);
	}
	//int ret =  sdhci_resume_host(host);
	/*
	 *Because resume host will write/read sdhci host register,
	 *so we must enable the bus clk and sd clk
	 */

	sdhci_nusmart_set_host_clock(plat,1);
	ret =  sdhci_resume_host(host);
	sdhci_nusmart_set_host_clock(plat,0);
	return ret;
}

#endif /*CONFIG_PM*/
#ifdef CONFIG_PM_RUNTIME
static void sdhci_nusmart_set_host_clock(struct nusmart_sdhci_platform_data *plat,int enable)
{
	if(enable){
		if(plat->ctype == EMMC_CARD){
			clk_prepare_enable(plat->bus_clk);
			clk_prepare_enable(plat->sd_clk);
		}else if((plat->ctype == SD_CARD) && plat->no_dp){
			clk_prepare_enable(plat->bus_clk);
			clk_prepare_enable(plat->sd_clk);
		}
	}else{
		if(plat->ctype == EMMC_CARD){
			clk_disable_unprepare(plat->sd_clk);
			clk_disable_unprepare(plat->bus_clk);
		}else if((plat->ctype == SD_CARD) && plat->no_dp){
			clk_disable_unprepare(plat->sd_clk);
			clk_disable_unprepare(plat->bus_clk);
		}
	}
}
static int sdhci_nusmart_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
	int ret;
	ret = sdhci_runtime_suspend_host(host);

	clk_disable_unprepare(plat->sd_clk);
	clk_disable_unprepare(plat->bus_clk);
	return ret;
}

static int sdhci_nusmart_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_nufront *nufront_host = pltfm_host->priv;
	struct nusmart_sdhci_platform_data *plat = (struct nusmart_sdhci_platform_data *)nufront_host->plat;
	int ret;

	clk_prepare_enable(plat->bus_clk);
	clk_prepare_enable(plat->sd_clk);
	ret =  sdhci_runtime_resume_host(host);
	return ret;
}
#endif /*CONFIG_PM_RUNTIME*/
static const struct of_device_id sdmmc_of_match[]  = {
	{.compatible = "nufront,sdhci-nusmart",},
	{},
};

MODULE_DEVICE_TABLE(of, sdmmc_of_match);

#ifdef CONFIG_PM
static const struct dev_pm_ops sdhci_nusmart_pmops = {
	.suspend	= sdhci_nusmart_suspend,
	.resume		= sdhci_nusmart_resume,
	SET_RUNTIME_PM_OPS(sdhci_nusmart_runtime_suspend,sdhci_nusmart_runtime_resume,NULL)
};
#define		SDHCI_NUSMART_PMOPS	(&sdhci_nusmart_pmops)

#else
#define		SDHCI_NUSMART_PMOPS	NULL
#endif

static struct platform_driver sdhci_nufront_driver = {
	.driver	= {
		.name = "sdhci-nusmart",
		.owner = THIS_MODULE,
		.of_match_table = sdmmc_of_match,
		.pm	= SDHCI_NUSMART_PMOPS,
	},
	.probe		= &sdhci_nufront_probe,
	.remove		= &sdhci_nufront_remove,
};

module_platform_driver(sdhci_nufront_driver);

MODULE_DESCRIPTION("SDHCI driver for Nufront");
MODULE_AUTHOR("Nufront, Inc.");
MODULE_LICENSE("GPL");
