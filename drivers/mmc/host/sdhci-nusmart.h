
#include <linux/types.h>
#include <linux/device.h>
#include <linux/mmc/host.h>


#define SD_CARD         1
#define MMC_CARD        2
#define EMMC_CARD       4
#define SDIO_CARD       8

struct device;
struct mmc_host;

struct sdhci_nufront_soc_data {
	struct sdhci_pltfm_data *pdata;
	u32 nvquirks;
};

struct sdhci_nufront {
	const struct nusmart_sdhci_platform_data *plat;
	const struct sdhci_nufront_soc_data *soc_data;
};

struct nusmart_sdhci_platform_data {
	struct clk *sd_clk;
	unsigned int ref_clk;
	unsigned int freq;
	unsigned int f_min;
	unsigned char *clk_name;
	unsigned char *pin_enable;

	int ctype;
	bool force_rescan;
	bool is_8bit;
	bool no_wp;
	bool no_dp;
	unsigned int pm_caps;
	u32 caps;
	u32 ocr_avail;
	u32 mmc_base;
	u32 drv_strength;
	u32 drv_strength2;
	int scm_offset;
	int scm_offset2;
	void __iomem *addr;
	int (*wifi_init)(void);
};
#ifdef CONFIG_BCMDHD
       extern int bcmdhd_wifi_init(void);
#else
       int bcmdhd_wifi_init(void)
       {
               return 0;
       }
#endif
