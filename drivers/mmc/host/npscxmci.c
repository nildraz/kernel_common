/*
 * Nufront MultiMedia Card Interface driver
 *
 * Copyright (C) 2015 Nufront Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/of.h>

#include <linux/of_gpio.h>
#include <linux/clk.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/slot-gpio.h>

#include "npscxmci.h"

#define  EMMC_MAX_CLK 96000000


#define		NPSCXMCI_TEST_ARCHIVES(host,event) \
	test_bit(event,&host->archives_events)

#define		NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,event) \
	test_and_clear_bit(event,&host->archives_events)

#define		NPSCXMCI_CLEAR_ARCHIVES(host,event) \
	clear_bit(event,&host->archives_events)

#define		NPSCXMCI_SET_ARCHIVES(host,event) \
	set_bit(event,&host->archives_events)
#define		NPSCXMCI_SET_ACCOMPLISHED(host,completed) \
	set_bit(completed,&host->accomplish_events)

#define		NPSCXMCI_TEST_AND_CLEAR_ACCOMPLISHED(host,completed) \
	test_and_clear_bit(completed,&host->accomplish_events)

#define		NPSCXMCI_SET_FLAGS(host,flag) \
	set_bit(flag,&host->flags)

#define		NPSCXMCI_TEST_AND_CLEAR_FLAGS(host,flag) \
	test_and_clear_bit(flag,&host->flags)

#define		NPSCXMCI_TEST_FLAGS(host,flag) \
	test_bit(flag,&host->flags)

#define		NPSCXMCI_CLREAR_FLAGS(host,flag) \
	clear_bit(flag,&host->flags)

#define		DMA_DESC_ADDR_ALIGN		4

#define		DMA_MAX_IDMAC_DESC_LEN		(1 << 13)

#define		derror				(1 << 0)
#define		ddebug				(1 << 1)
#define		dwarn				(1 << 2)
#define		dinfo				(1 << 3)
#define		DEBUG_MASK			(0x07)
#define		npscxmci_debug(host,debug,fmt,arg...) \
	do{					\
		if(debug & DEBUG_MASK){				\
			switch (debug & DEBUG_MASK){		\
				case	derror:			\
					dev_err(&(host)->pdev->dev,fmt,##arg); \
				break;			\
				case	dinfo:			\
					dev_info(&(host)->pdev->dev,fmt,##arg);	\
					break;			\
				case	ddebug:			\
					dev_dbg(&(host)->pdev->dev,fmt,##arg);	\
					break;				\
				case	dwarn:			\
					dev_warn(&(host)->pdev->dev,fmt,##arg);		\
					break;				\
			}			\
		}				\
	}while(0)				\



static void npscxmci_dump_registers(struct npscxmci_host *host);
static struct list_head	npscxmci_softplug_list;

const static char *npscxmci_cmd_fsm_status[16] = {
	"idle",
	"Send init sequence",
	"Tx cmd start bit",
	"Tx cmd tx bit",
	"Tx cmd index + arg",
	"Tx cmd crc7",
	"Tx cmd end bit",
	"Rx resp start bit",
	"Rx resp IRQ response ",
	"Rx resp tx bit",
	"Rx resp cmd idx",
	"Rx resp data",
	"Rx resp crc7",
	"Rx resp end bit",
	"Cmd path wait NCC",
	"Wait;CMD-to-response turnaroud",
};

const static char *npscxmci_dmac_fsm_status[9] = {
	"DMA idle",
	"DMA suspend",
	"Descriptor Read",
	"Descriptor check",
	"DMA read req wait",
	"DMA write req wait",
	"DMA read",
	"DMA write",
	"Descriptor close",
};

static void inline npscxmci_clear_set(struct npscxmci_host *host,unsigned int reg,unsigned mask,unsigned value)
{
	u32 v = readl(host->iobase + reg);
	v &= ~mask;
	v |= value;
	writel(v,host->iobase + reg);
}

static bool  npscxmci_poll_status(struct npscxmci_host *host,unsigned int reg,unsigned int poll_bits,bool clear,unsigned int ms_tmout)
{
	u32 status;
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	if(ms_tmout > 0){
		while(time_before(jiffies,timeout)){
			status = readl(host->iobase + reg);
			if(clear){
				/*
				 * wait for the poll_bits changes to zero
				 */
				if(!(status & poll_bits))
					return true;
			}else{
				/*
				 *wait for the poll_bits changes to 1
				 */
				if((status & poll_bits ) == poll_bits)
					return true;
			}

		}
		return false;
	}else{
		status = readl(host->iobase + reg);
		if(clear){
			/*
			* wait for the poll_bits changes to zero
			*/
			if(!(status & poll_bits))
				return true;
		}else{
			/*
			*wait for the poll_bits changes to 1
			*/
			if((status & poll_bits) == poll_bits)
				return true;
		}

		return false;

	}

}

static int npscxmci_config_clk_cmd(struct npscxmci_host *host ,unsigned int cmdarg,unsigned int ms_tmout)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	npscx_writel(host,CMD,cmdarg);
	do{
		if(time_after(jiffies,timeout)){
			return -ETIMEDOUT;
		}
	}while(npscx_readl(host,CMD) & CMD_START);
	return 0;
}


static bool npscxmci_reset_fifo(struct npscxmci_host *host,unsigned int ms_tmout)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	//npscx_writel(host,CTRL,CTRL_FIFO_RST);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_FIFO_RST,CTRL_FIFO_RST);
	if(ms_tmout > 0){
		do{
			if(time_after(jiffies , timeout))
				return false;
		}while(npscx_readl(host,CTRL) & CTRL_FIFO_RST);
	}
	return true;
}

static bool npscxmci_reset_dma(struct npscxmci_host *host,unsigned int ms_tmout,bool soft)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	/*
	 *reset DMA Interface
	 */
	//npscx_writel(host,CTRL,CTRL_DMA_RST);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_DMA_RST,CTRL_DMA_RST);
	if(ms_tmout > 0){
		do{
			if(time_after(jiffies ,  timeout))
				return false;
		}while(npscx_readl(host,CTRL) & CTRL_DMA_RST);
	}

	/*
	 *soft reset dma controller
	 */
	if(soft)
		npscx_writel(host,BMOD,BMOD_DMA_RST);
	return true;

}

static bool npscxmci_reset_host(struct npscxmci_host *host,unsigned int ms_tmout)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	//npscx_writel(host,CTRL,CTRL_HOST_RST);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_HOST_RST,CTRL_HOST_RST);
	if(ms_tmout > 0){
		do{
			if(time_after(jiffies , timeout))
				return false;
		}while(npscx_readl(host,CTRL) & CTRL_HOST_RST);
	}
	return true;
}
static int npscxmci_of_parse(struct npscxmci_host  *host,struct npscxmci_pltfm_data *p)
{
	struct device_node *np;
	int gpio;
	enum of_gpio_flags flags;
	int len;
	int result;
	int bus_width;
	int gpio_irq;
	u32 ocr_avail;
	struct mmc_host *mmc;
	struct npscxmci_pltfm_data *private = host->prv_data;
	np = host->pdev->dev.of_node;
	mmc = host->mmc;

	if(!np){
		npscxmci_debug(host,derror,"%s: can't find the device node of %s\n",__func__,dev_name(&host->pdev->dev));
		return -ENODEV;
	}
	if(of_property_read_u32(np,"npscxmci,card-type",&private->type) < 0){
		npscxmci_debug(host,derror,"\"card -type\" is missing\n");

		return -EINVAL;

	}
	npscxmci_debug(host,ddebug,"ctype = %x\n",private->type);
	if(of_property_read_u32(np,"npscxmci,bus-width",&bus_width) < 0){
		npscxmci_debug(host,derror,"\"bus-width\" is missing,assuming 4 bit\n");

		bus_width = 4;
	}
	npscxmci_debug(host,ddebug,"bus_width = %d\n",bus_width);
	switch(bus_width){
		case 8:
			mmc->caps |= MMC_CAP_8_BIT_DATA;
			break;
		case 4:
			mmc->caps |= MMC_CAP_4_BIT_DATA;
			break;
		case 1:
			break;
		default:
			npscxmci_debug(host,derror,"%s: unsuport the bus width[%d] \n",__func__,bus_width);
			return -EOPNOTSUPP;
	}
	if(of_property_read_u32(np,"npscxmci,ocr-avail",&ocr_avail) < 0){
		npscxmci_debug(host,dwarn,"\" ocr_avail\" is missing,assuming 0x00ff8000\n");

		ocr_avail = 0x00ff8000;
	}
	mmc->ocr_avail = ocr_avail;
	if(of_property_read_u32(np,"npscxmci,fifo-depth",&host->fifo_depth) < 0){
		npscxmci_debug(host,ddebug,"\"fifo depth\" is missing,assuming 2048\n");
		host->fifo_depth = 2048;
	}

	if(of_property_read_u32(np,"npscxmci,clock-source",&host->clk_src)  < 0){
		npscxmci_debug(host,derror,"\"clock source \" is missing \n");
		return -EINVAL;

	}
	if(of_property_read_u32(np,"npscxmci,max-frequency",&mmc->f_max) < 0){
		npscxmci_debug(host,derror,"\"max -frequency\" is missing,assuming clock in\n");
		mmc->f_max =  host->clk_src;
	}
	if(of_property_read_u32(np,"npscxmci,min-frequency",&mmc->f_min) < 0){
		npscxmci_debug(host,derror,"\" min - frequency\" is missing,assuming 400000\n");
		mmc->f_min = 400000;
	}

	/*
	 * mmc caps parse
	 */
	mmc->caps |= MMC_CAP_ERASE | MMC_CAP_CMD23;
	if(of_find_property(np,"npscxmci,noremoval",&len))
		mmc->caps |= MMC_CAP_NONREMOVABLE;
	if(of_find_property(np,"npscxmci,cap-highspeed",&len))
		mmc->caps |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
	if(of_find_property(np,"npscxmci,cap-sdr25",&len))
		mmc->caps |= MMC_CAP_UHS_SDR25;
	if(of_find_property(np,"npscxmci,cap-sdr104",&len))
		mmc->caps |= MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR12;
	if(of_find_property(np,"npscxmci,cap-sdr50",&len))
		mmc->caps |= MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR12;
	if(of_find_property(np,"npscxmci,cap-ddr50",&len))
		mmc->caps |= MMC_CAP_UHS_DDR50;
	if(of_find_property(np,"npscxmci,cap-1v8-ddr",&len))
		mmc->caps |= MMC_CAP_1_8V_DDR;
	if(of_find_property(np,"npscxmci,cap-1v2-ddr",&len))
		mmc->caps |= MMC_CAP_1_2V_DDR;
	if(of_find_property(np,"npscxmci,cap-needs-poll",&len))
		mmc->caps |= MMC_CAP_NEEDS_POLL;

	if(of_find_property(np,"npscxmci,cap-sdio-irq",&len))
		mmc->caps |= MMC_CAP_SDIO_IRQ;
	/**
	 *mmc caps2 parse
	 */
	if(of_find_property(np,"npscxmci,cap-hs200-1v8",&len))
		mmc->caps2 |= MMC_CAP2_HS200_1_8V_SDR | MMC_CAP2_HS200;
	if(of_find_property(np,"npscxmci,cap-hs200-1v2",&len))
		mmc->caps2 |= MMC_CAP2_HS200_1_2V_SDR | MMC_CAP2_HS200;
	//if(of_find_property(np,"npscxmci,cap-hs200-sdr",&len))
	//	mmc->caps2 |= MMC_CAP2_HS200;

	if(of_find_property(np,"npscxmci,cap-hs400-1v8",&len))
		mmc->caps2 |= MMC_CAP2_HS400_1_8V;
	if(of_find_property(np,"npscxmci,cap-hs400-1v2",&len))
		mmc->caps2 |= MMC_CAP2_HS400_1_2V;

	if(of_find_property(np,"npscxmci,cap-packed-cmd",&len))
		mmc->caps2 |= MMC_CAP2_PACKED_CMD;

	if(of_find_property(np,"npscxmci,cap-ena-sdio-wakeup",&len))
		mmc->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;

	if(of_find_property(np,"npscxmci,cap-keep-power",&len))
		mmc->pm_caps |= MMC_PM_KEEP_POWER;
	if(of_find_property(np,"npscxmci,cap-ingnor-pm-notify",&len))
		mmc->pm_caps |= MMC_PM_IGNORE_PM_NOTIFY;

	gpio = of_get_named_gpio_flags(np,"wp-gpios",0,&flags);
	private->wp_pin.gpio = gpio;
	if(gpio_is_valid(gpio)){
		if(flags & OF_GPIO_ACTIVE_LOW)
			private->wp_pin.active_low = true;

		result = devm_gpio_request_one(&mmc->class_dev, gpio, GPIOF_DIR_IN,"wp-pin");
		if(result){
			npscxmci_debug(host,derror,"%s : request gpio for wp-pin failed : %d\n",__func__,result);
			return result;
		}

		//private->wp_pin.gpio = gpio;

	}
	gpio = of_get_named_gpio_flags(np,"cd-gpios",0,&flags);
	private->cd_pin.gpio = gpio;
	if(gpio_is_valid(gpio)){
		if(flags & OF_GPIO_ACTIVE_LOW)
			private->cd_pin.active_low = true;
		//private->cd_pin.gpio = gpio;
		result = devm_gpio_request_one(&mmc->class_dev, gpio, GPIOF_DIR_IN,"cd-pin");
		if(result){
			npscxmci_debug(host,derror,"%s : request gpio for cd-pin failed : %d\n",__func__,result);
			return result;
		}

		gpio_irq = gpio_to_irq(gpio);
		if(gpio_irq >= 0){
			private->irq_cd = gpio_irq;
		}else
			private->irq_cd = -ENXIO;
	}


	if(of_find_property(np,"npscxmci,broken-timeout",&len))
		NPSCXMCI_SET_FLAGS(host,FLAGS_BROKEN_TIMEOUT);



	return 0;

}
static bool npscxmci_reset_fsm(struct npscxmci_host *host,unsigned int ms_tmout)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms_tmout);
	npscx_writel(host,CTRL,(CTRL_HOST_RST | CTRL_FIFO_RST | CTRL_DMA_RST));
	if(ms_tmout > 0){
		do{
			if(time_after(jiffies , timeout))
				return false;
		}while(npscx_readl(host,CTRL) & (CTRL_HOST_RST | CTRL_FIFO_RST | CTRL_DMA_RST));
	}
	return true;

}
static int npscxmci_init(struct npscxmci_host *host)
{
	u32 fifo_size;
	u32 tmp;
	npscxmci_reset_fsm(host,500);
	/*
	 *reset dma again for soft reset.
	 */
	npscxmci_reset_dma(host,500,true);


	npscx_writel(host,CLKENA,0);
	npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
	npscx_writel(host,CLKDIV,0);
	//npscx_writel(host,CLKSRC,0);
	npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
	npscx_writel(host,TMOUT,0xffffff40);
	npscx_writel(host,CTYPE,0);
	/*clear all pending interrupts*/
	npscx_writel(host,RINTSTS,0xffffffff);
	/*Mask all interrupts*/
	npscx_writel(host,INTMASK,0x0);
	/*
	 * clear all pendings interrupts for DMA.
	 */
	npscx_writel(host,IDSTS,0x3ff);

	/**
	 *disable dma interrupts
	 */
	npscx_writel(host,IDINTEN,0x0);

	if(host->fifo_depth){
		npscx_writel(host,FIFOTH,((/*host->dma_msize*/0x2 << 28) | (((host->fifo_depth / 2 - 1) & 0xfff) << 16) | ((host->fifo_depth / 2 ) & 0xfff)));

	}else{
		fifo_size = ((npscx_readl(host,FIFOTH)  >> 16 ) & 0xfff) + 1; //rxwmark defalut is fifo_depth - 1;
		host->fifo_depth = fifo_size;
		tmp = 0x2 << 28 | (((fifo_size / 2 - 1) & 0xfff) << 16) | ((fifo_size / 2) & 0xfff);
		npscx_writel(host,FIFOTH,tmp);

	}


	/*enable interrupts*/
	npscxmci_clear_set(host,NPSCXMCI_INTMASK,INT_INITIA_ENABLE,INT_INITIA_ENABLE);
	npscxmci_debug(host,derror,"intmask = %x\n",npscx_readl(host,INTMASK));
	/*
	 * enable interrupt
	 */
	//npscx_writel(host,CTRL,CTRL_INT_ENABLE);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_INT_ENABLE,CTRL_INT_ENABLE);
	return 0;
}


static int npscxmci_disable_contrl(struct npscxmci_host *host)
{


	npscxmci_reset_fsm(host,500);

	npscx_writel(host,CLKENA,0);
	npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
	npscx_writel(host,CLKDIV,0);
	//npscx_writel(host,CLKSRC,0);
	npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);

	/*clear all pending interrupts*/
	npscx_writel(host,RINTSTS,0xffffffff);
	/*Mask all interrupts*/
	npscx_writel(host,INTMASK,0x0);
	/*
	 * clear all pendings interrupts for DMA.
	 */
	npscx_writel(host,IDSTS,0x3ff);

	/**
	 *disable dma interrupts
	 */
	npscx_writel(host,IDINTEN,0x0);
	/*
	 *disable global interrupt
	 */
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_INT_ENABLE,0);
	return 0;
}

/***
 *
 * The size of data buffer for each descriptor must
 * be a multiple of MSIZE * H_DATA_WIDTH / 8
 * when enable threshold(r/w) and in idmac mode.
 */
static inline bool npscxmci_adjust_msize(struct npscxmci_host *host,int msize)
{
	if(NPSCXMCI_TEST_FLAGS(host,FLAGS_USE_DMA_TRAN)){
		int i;
		struct scatterlist *sg;
		struct mmc_data *data = host->data;
		bool thold_use;
		if(data->flags & MMC_DATA_READ)
			thold_use = host->r_thold_use & host->blksz_allign_dword;
		else
			thold_use = host->w_thold_use;
		if(!thold_use)
			return true;
		for_each_sg(host->data->sg,sg,host->sg_cnt,i){
			if(sg_dma_len(sg) % (msize * 4))
				return false;

		}
		return true;
	}
	return true;
}
static void npscxmci_config_threshold_fifo(struct npscxmci_host *host)
{
	struct mmc_data *data;
	//BUG_ON(!host->data);
	int msz[] = {1,4,8,16,32,64,128,256};
	int index;
	int blksz;
	int blksize;
	int msize_idx = 0;
	int fifo_width;
	int i;
	int txwmark,rxwmark,threshold_sz = 512;
	bool thold_use = false;
	u32 value;
	txwmark = host->fifo_depth / 2;
	rxwmark = host->fifo_depth / 2 - 1;

	if(!host->data){
		npscxmci_debug(host,dwarn,"%s : There is no need to config threshold because data is empty\n",__func__);
		return;
	}
	data = host->data;
	blksz = data->blksz;
	index = sizeof(msz) / sizeof(int);
	/**
	 * Blksize = (block size) * 8 / F_DATA_WIDTH
	 *
	 */
	blksize = blksz * 8 / 32;
	fifo_width = 32 / 8;
	if(blksz % fifo_width){

		msize_idx = 0;
	}else{
		for(i = index -1 ; i >=0; i--){
			if((blksize % msz[i] == 0) && (npscxmci_adjust_msize(host,msz[i]))){
				msize_idx = i;
				break;
			}
		}
	}
	if(data->flags & MMC_DATA_READ){
		if((msize_idx != 0) && (NPSCXMCI_TEST_FLAGS(host,FLAGS_USE_DMA_TRAN)))
			rxwmark = msz[i] - 1 ;
		threshold_sz = blksz;
		thold_use = host->r_thold_use & host->blksz_allign_dword;
	}else if((data->flags & MMC_DATA_WRITE) && host->w_thold_use){
		txwmark = blksize;
		threshold_sz = blksz;
		thold_use = host->w_thold_use; // only hs400 use write threshold,then blocksize is 512 bytes in hs400
	}

	value = (msize_idx << 28) | ((rxwmark & 0xfff) << 16) | (txwmark & 0xfff);
	npscx_writel(host,FIFOTH,value);
	if(thold_use){
		if(data->flags & MMC_DATA_READ){
			value = npscx_readl(host,CARDTHRCTL);
			value &= ~(0xfff << 16 | CARDTHRCTL_WEN | CARDTHRCTL_REN );
			value |= ((threshold_sz & 0xfff) << 16) | CARDTHRCTL_REN;
			npscx_writel(host,CARDTHRCTL,value);
		}else{

			value = npscx_readl(host,CARDTHRCTL);
			value &= ~(0xfff << 16 | CARDTHRCTL_WEN | CARDTHRCTL_REN);
			value |= ((threshold_sz & 0xfff) << 16) | CARDTHRCTL_WEN;
			npscx_writel(host,CARDTHRCTL,value);
		}
	}

}
static void npscxmci_bulid_dma_chain(struct npscxmci_host *host)
{
	int i = 0;
	struct mmc_data *data;
	struct	npscxmci_dma_desc *p,*q;
	dma_addr_t	addr;
	dma_addr_t	chain_addr,chain_addr_first;
	int len;
	if(!host->data){
		npscxmci_debug(host,dwarn,"%s : no data to for dma transfer\n",__func__);
		return;
	}
	p = host->dma_desc_vaddr;
	chain_addr = host->dma_desc_paddr;

	p = (struct npscxmci_dma_desc *)p;
	data = host->data;
	q = p;
	chain_addr_first = chain_addr;

	for(i = 0; i < host->sg_cnt; i++,q++){
		addr = sg_dma_address(&data->sg[i]);
		len = sg_dma_len(&data->sg[i]);

		q->des0 = (DES0_OWN | DES0_CH | DES0_DIC);
		if(i == 0)
			q->des0 |= DES0_FS;
		if(i == (host->sg_cnt - 1))
			q->des0 |= DES0_LD;

		q->des1 = DES1_BS1_SIZE(len);
		q->des2 = addr;
		if(i == (host->sg_cnt - 1))
			q->des0 &= ~(DES0_DIC );


		//npscxmci_debug(host,derror,"des0 = %x,desc1= %x,desc2=%x,desc3=%x\n",q->des0,q->des1,q->des2,q->des3);

	}
	/*
	 * write the first dma descriptor addr into DBADDR register.
	 */
	npscx_writel(host,DBADDR,chain_addr_first);

}
/**
 *npscxmci_prepare_dma,npscxmci_start_dma,npscxmci_stop_dma etc are for DMA data transfer.
 * */
static int npscxmci_prepare_dma(struct npscxmci_host *host ,struct mmc_command *cmd)
{
	int direction;
	struct mmc_data *data = cmd->data;
	if(!data)
		return -EINVAL;
	if(data->flags & MMC_DATA_WRITE)
		direction = DMA_TO_DEVICE;
	else
		direction = DMA_FROM_DEVICE;

	host->sg_cnt = dma_map_sg(mmc_dev(host->mmc),data->sg,data->sg_len,direction);
	if(!host->sg_cnt)
		return -EINVAL;
#if 0
	dma_desc_len = host->sg_cnt * sizeof(struct npscxmci_dma_desc) + DMA_DESC_ADDR_ALIGN;
	host->dma_desc_vaddr = dma_alloc_coherent(mmc_dev(host->mmc),dma_desc_len,&host->dma_desc_paddr,GFP_KERNEL);
	if(IS_ERR(host->dma_desc_vaddr))
		return PTR_ERR(host->dma_desc_vaddr);
	host->dma_desc_length = dma_desc_len;
#endif
	npscxmci_bulid_dma_chain(host);

	return 0;

}


/**
 *DMA descriptor init
 */

static int npscxmci_dma_desc_setup(struct npscxmci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	u32	dma_size = 0;
	int	i;
	dma_addr_t allign_addr;
	struct	npscxmci_dma_desc *desc;
	BUG_ON(!mmc);

	dma_size = DMA_MAX_IDMAC_DESC_LEN;
	host->dma_desc_vaddr = dma_alloc_coherent(mmc_dev(mmc),dma_size,&host->dma_desc_paddr,GFP_KERNEL);
	if(IS_ERR_OR_NULL(host->dma_desc_vaddr)){
		npscxmci_debug(host,derror,"alloc dma failed ,size = %d\n",dma_size);
		return -ENOMEM;
	}
	if(unlikely(host->dma_desc_paddr & (DMA_DESC_ADDR_ALIGN - 1))){
		dma_free_coherent(mmc_dev(mmc),dma_size,host->dma_desc_vaddr,host->dma_desc_paddr);
		dma_size = DMA_DESC_ADDR_ALIGN + DMA_MAX_IDMAC_DESC_LEN;
		host->dma_desc_vaddr = dma_alloc_coherent(mmc_dev(mmc),dma_size,&host->dma_desc_paddr,GFP_KERNEL);
		if(IS_ERR_OR_NULL(host->dma_desc_vaddr)){
			npscxmci_debug(host,derror,"alloc dma failed,size = %d\n",dma_size);
			return -ENOMEM;
		}
	}
	host->dma_desc_length = dma_size;
	if(unlikely(host->dma_desc_paddr & (DMA_DESC_ADDR_ALIGN - 1))){
		npscxmci_debug(host,dwarn,"The dma descriptor start addr is not allign 4 bytes");
		allign_addr = (host->dma_desc_paddr + (DMA_DESC_ADDR_ALIGN - 1)) & ~(DMA_DESC_ADDR_ALIGN - 1);
		host->dma_allign_pad = allign_addr - host->dma_desc_paddr;
		host->dma_desc_vaddr =(u32)host->dma_desc_vaddr + host->dma_allign_pad;
		host->dma_desc_paddr = allign_addr;

	}

	desc = host->dma_desc_vaddr;
	for(i = 0;i < DMA_MAX_IDMAC_DESC_LEN / sizeof(struct npscxmci_dma_desc) - 1;i++,desc++)
		desc->des3 = host->dma_desc_paddr + sizeof(struct npscxmci_dma_desc) * (i + 1);
	desc->des3 = host->dma_desc_paddr;
	desc->des0 = DES0_ER;

	return 0;



}

static int npscxmci_start_dma(struct npscxmci_host *host,struct mmc_command *cmd)
{
	struct mmc_data *data;
	u32 tmp = 0;
	data = cmd->data;
	if(!data){
		npscxmci_debug(host,dwarn,"%s : the cmd%d is not data trasfer command\n",__func__,cmd->opcode);
		return -EIO;
	}
	/**
	 * Enable dma interface
	 * */
	//npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_DMA_ENABLE,CTRL_DMA_ENABLE);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_USE_IDMAC,CTRL_USE_IDMAC);
	/* *
	 * Enable dma controller
	 */
	npscx_writel(host,PLDMND,PLDMND_VALUE);
	tmp = BMOD_FB | BMOD_IDMA_EN;
	npscxmci_clear_set(host,NPSCXMCI_BMOD,tmp,tmp);

	/*
	 *Diable pio interrupts
	 */
	npscxmci_clear_set(host,NPSCXMCI_INTMASK,(DATA_READ_MASK | DATA_WRITE_MASK),0);
	/*
	 *Maybe should enable dma_ti and dma_ri interrupts.
	 */

	return 0;

}

static int  npscxmci_stop_dma(struct npscxmci_host *host,struct mmc_command *cmd)
{
	struct mmc_data *data;
	data = cmd->data;

	if(!data){
		npscxmci_debug(host,dwarn,"%s : the cmd%d is not data transfer command\n",__func__,cmd->opcode);
		return -EIO;
	}
	/**
	 *Diable dma interface
	 * */
	//npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_DMA_ENABLE,0);
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_USE_IDMAC,0);
	/*
	 *Disable dma controller
	 */
	npscxmci_clear_set(host,NPSCXMCI_BMOD,BMOD_IDMA_EN,0);

	return 0;

}

static int npscxmci_dmamode_reset_fsm(struct npscxmci_host *host)
{
	/**
	 *
	 */
	unsigned result;
	switch(host->dma_intf){
		case DW_DMA_INTF:
		case NON_DW_INTF:
			npscxmci_reset_host(host,100);
			npscxmci_reset_fifo(host,100);
			result = npscxmci_poll_status(host,NPSCXMCI_STATUS,STATUS_DMA_REQ,false,100);
			npscxmci_debug(host,ddebug,"%s : poll dma reg to zero : %s",__func__,result > 0 ? "sucess" :"failed");
			npscxmci_reset_fifo(host,100);
			/*
			 *Clear pending interrupts
			 */
			npscxmci_clear_set(host,NPSCXMCI_RINTSTS,-1,-1);
			break;
		case GEN_DMA_INTF:
			//npscxmci_reset_host(host,100);
			npscxmci_reset_dma(host,100,true);
			npscxmci_reset_fifo(host,100);
			/*
			 *Clear pending interrupts
			 */
			npscxmci_clear_set(host,NPSCXMCI_RINTSTS,-1,-1);
			break;
		default:
			npscxmci_debug(host,dwarn,"%s : NON DMA MODE for reset \n",__func__);
			break;

	}
	return 0;

}
static void npscxmci_dma_cleanup(struct npscxmci_host *host)
{
	struct mmc_data *data = host->data;
	int direction;

	if(!data){
		npscxmci_debug(host,dwarn,"%s : here is not need to clearup dma ,because no data to transfer\n",__func__);
		return;
	}
	if(data->flags & MMC_DATA_WRITE)
		direction = DMA_TO_DEVICE;
	else
		direction = DMA_FROM_DEVICE;
	dma_unmap_sg(mmc_dev(host->mmc),data->sg,host->sg_cnt,direction);
#if 0
	dma_free_coherent(&host->pdev->dev,host->dma_desc_length,host->dma_desc_vaddr,host->dma_desc_paddr);
#endif

}

/**
 *npscxmci_prepare_pio,npscxmci_start_pio,npscxmci_stop_pio etc are for pio data transfer.
 * */

static int  npscxmci_prepare_pio(struct npscxmci_host *host,struct mmc_command *cmd)
{
	struct mmc_data *data;
	int	flags;
	data = cmd->data;
	if(!data)
		return -EINVAL;

	flags = SG_MITER_ATOMIC;
	if(data->flags & MMC_DATA_WRITE)
		flags |= SG_MITER_FROM_SG;
	else
		flags |= SG_MITER_TO_SG;
	sg_miter_start(&host->sg_miter,data->sg,data->sg_len,flags);

	return 0;

}

static int npscxmci_start_pio(struct npscxmci_host *host,struct mmc_command *cmd)
{
	struct mmc_data *data;
	data = cmd->data;

	if(!data){
		npscxmci_debug(host,dwarn,"%s : the cmd%d is not data trasfer command\n",__func__,cmd->opcode);
		return -EIO;
	}
	/**
	 * Disable dma interface
	 * */
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_USE_IDMAC,0);
	/* *
	 * Disable dma controller
	 */
	npscxmci_clear_set(host,NPSCXMCI_BMOD,BMOD_IDMA_EN | BMOD_FB,0);

	/*
	 *Enable pio interrupts
	 */
	npscxmci_clear_set(host,NPSCXMCI_INTMASK,(DATA_READ_MASK | DATA_WRITE_MASK),(DATA_READ_MASK | DATA_WRITE_MASK));

	return 0;
}

static int npscxmci_stop_pio(struct npscxmci_host *host,struct mmc_command *cmd)
{
	struct mmc_data *data;
	data = cmd->data;
	if(!data){
		npscxmci_debug(host,dinfo,"%s :the command%d is not data command\n",__func__,cmd->opcode);
		return -EIO;
	}
	npscxmci_debug(host,dinfo,"%s : onething to do\n",__func__);

	sg_miter_stop(&host->sg_miter);
	return 0;
}


static int npscxmci_command_reset_fsm(struct npscxmci_host *host)
{
	/**
	 *when command error happen,we don't need reset host.
	 */
	//npscxmci_reset_host(host,100);
	npscxmci_clear_set(host,NPSCXMCI_RINTSTS,0xffffffff,0xffffffff);
	return 0;
}
static int npscxmci_piomode_reset_fsm(struct npscxmci_host *host)
{
	/*
	 *reset controller
	 */
	//npscxmci_reset_host(host,100);
	/*
	 *reset fifo.
	 */
	npscxmci_reset_fifo(host,100);
	/*
	 *Clear pending interrupts
	 */
	npscxmci_clear_set(host,NPSCXMCI_RINTSTS,-1,-1);
	return 0;
}


static void npscxmci_pio_cleanup(struct npscxmci_host *host)
{
	struct mmc_data *data = host->data;

	if(!data){
		npscxmci_debug(host,dwarn,"%s : here is not need to clearup dma ,because no data to transfer\n",__func__);
		return;

	}

	return;

}

static inline u32 npscxmci_get_fifocnt(struct npscxmci_host *host)
{
	u32 fifo_locations = npscx_readl(host,STATUS);

	fifo_locations >>= 17;
	fifo_locations &= STATUS_FIFO_CNT_MASK;
	/*
	 * the fifo data width is 32bits,so every location is 4 bytes.
	 * so we should left shift 2.
	 */
	return fifo_locations << 2;
}
static int npscxmci_pio_read(struct npscxmci_host *host,bool last_tran)
{
	u32 value,len;
	int result = 0;
	u32 fifo_cnt;
	u32 dwords;
	u32 status = 0;
	u8  *buf;
	int i = 0;
	u32 last_remain = 0;
	/**
	 * when read data from card, controller will generate DTO and DRTO and
	 * the DTO interrupt maybe be pended when CPU handles the DRTO interrupt,
	 * so the state_tasklet will be handled after DTO interrupt,we must aovoid
	 * enter an infinite loop,so we should return.
	 */
	if(NPSCXMCI_TEST_ARCHIVES(host,EVENT_DATA_ERROR) && last_tran){
		npscxmci_debug(host,derror,"we should't read data when data's errors have happened\n");
		return -EFAULT;
	}

	do{
		fifo_cnt = npscxmci_get_fifocnt(host);

		while(fifo_cnt){
			if(!sg_miter_next(&host->sg_miter))
				goto done;
			len = min(host->sg_miter.length,fifo_cnt);
			fifo_cnt -= len;
			host->sg_miter.consumed = len;
			buf = (u8 *)host->sg_miter.addr;
			if(last_remain > 0){
				npscxmci_debug(host,derror,"cmd%d have last_remain data\n",host->cmd->opcode);
				for(i = 0; i < (4 - last_remain);i++){
					*buf = value & 0xff;
					buf++;
					value >>= 8;
				}
				len -= (4 - last_remain);
				last_remain = 0;
				value = 0;

			}
			dwords = len >> 2;
			for(i = 0; i < dwords;i++){

				*(u32 *)buf = npscx_readl(host,DATA_FIFO);
				buf += 4;
			}
			last_remain = len & 3;
			if(last_remain > 0){
				value = npscx_readl(host,DATA_FIFO);
				memcpy(buf,&value,last_remain);
				value >>= (last_remain * 8);
				buf += last_remain;
			}

		}

		status = npscx_readl(host,MINTSTS);
		if(status & DATA_READ_MASK)
			npscx_writel(host,RINTSTS,(status & DATA_READ_MASK));

		/*
		 * Here should check data error ;TO DO
		 */
		if(status & DATA_READ_ERROR_MASK){
			npscx_writel(host,RINTSTS,status & DATA_READ_ERROR_MASK);
			host->data_archives |= status & DATA_READ_ERROR_MASK;
			smp_wmb();
			NPSCXMCI_SET_ARCHIVES(host,EVENT_DATA_ERROR);
			tasklet_schedule(&host->state_tasklet);
			result = -EFAULT;
			break;

		}

		if(!sg_miter_next(&host->sg_miter))
			goto done;
		else{
			host->sg_miter.consumed = 0;
			sg_miter_stop(&host->sg_miter);
		}
		if(last_tran)
			npscxmci_debug(host,dinfo,"%s [%d] ,last_tran = %d \n",__func__,__LINE__,last_tran);

	}while((status & DATA_READ_MASK) || last_tran);


	return result;
done:
	sg_miter_stop(&host->sg_miter);
	return 0;

}

static int npscxmci_pio_write(struct npscxmci_host *host)
{
	u32 value,len;
	int result = 0;
	u32 fifo_cnt;
	u32 dwords;
	u32 status = 0;
	u8  *buf;
	int i = 0;
	u32 last_remain = 0;
	do{
		fifo_cnt = (host->fifo_depth << 2) -  npscxmci_get_fifocnt(host);

		while(fifo_cnt){
			if(!sg_miter_next(&host->sg_miter))
				goto done;
			len = min(host->sg_miter.length,fifo_cnt);
			fifo_cnt -= len;
			host->sg_miter.consumed = len;
			buf = (u8 *)host->sg_miter.addr;
			if(last_remain > 0){
				for(i = 0; i < (4 -last_remain);i++){

					value |= *buf << ((last_remain + i) * 8);
					buf++;

				}
				npscx_writel(host,DATA_FIFO,value);

				value = 0;
				len -= (4 - last_remain);
				last_remain = 0;
			}
			dwords = len >> 2;
			for(i = 0; i < dwords;i++){
				npscx_writel(host,DATA_FIFO,*(u32 *)buf);

				buf += 4;
			}
			last_remain = len & 3;
			if(last_remain > 0){

				memcpy(&value,buf,last_remain);
				buf += last_remain;
				//value >>= last_remain * 8;
			}

		}
		status = npscx_readl(host,MINTSTS);
		if(status & DATA_WRITE_MASK)
			npscx_writel(host,RINTSTS,(status & DATA_WRITE_MASK));
		/*
		 * Here should check data error ;TO DO
		 */
		if(status & DATA_WRITE_ERROR_MASK){
			npscx_writel(host,RINTSTS,status & DATA_WRITE_ERROR_MASK);
			host->data_archives |= status & DATA_WRITE_ERROR_MASK;
			smp_wmb();
			NPSCXMCI_SET_ARCHIVES(host,EVENT_DATA_ERROR);
			tasklet_schedule(&host->state_tasklet);
			result = -EFAULT;
			break;
		}

		if(!sg_miter_next(&host->sg_miter))
			goto done;
		else{
			host->sg_miter.consumed = 0;
			sg_miter_stop(&host->sg_miter);
		}

	}while(status & DATA_WRITE_MASK);

	return result;
done:
	/**
	 *
	 */
	if(unlikely(last_remain > 0))
		npscx_writel(host,DATA_FIFO,value);
	sg_miter_stop(&host->sg_miter);
	return 0;

}

static u32 npscxmci_config_timeout(struct npscxmci_host *host,struct mmc_command *cmd)
{
	u32 tmout_clk;
	u32 us_to_clks;
	u32 us_tmout;
	struct mmc_data *data;

	data = cmd->data;
	if(NPSCXMCI_TEST_FLAGS(host,FLAGS_BROKEN_TIMEOUT))
	{
		tmout_clk = 0xffffff40;
		//npscx_writel(host,TMOUT,tmout_clk);
		goto done;

	}
	if(!data && !cmd->cmd_timeout_ms){

		tmout_clk =  0xffffff40;
		goto done;
	}


	if(!data)
		us_tmout = cmd->cmd_timeout_ms * 1000;
	else
		us_tmout = data->timeout_ns / 1000;

	//us_to_clks = (us_tmout / 1000000) * (1 / host->clock);
	us_to_clks = DIV_ROUND_UP((us_tmout * host->clock),1000000);
	us_to_clks += data->timeout_clks;
	tmout_clk = (us_to_clks << 8) | 0x40;
done:

	npscx_writel(host,TMOUT,tmout_clk);

	return tmout_clk;

}

static int set_dll_config(struct npscxmci_host *host,int master_val, int data_stb, int clk_sample, int clk_drv)
{
	unsigned int reg_val;
	unsigned int delay_count = 10;

	reg_val = readl(host->sd_clk_ctrl_vaddr);
	reg_val &= ~EMMC_DLL_OUT_EN;
	writel(reg_val, host->sd_clk_ctrl_vaddr);

	reg_val = npscx_readl(host, SDMMC_RESET_DLL);
	reg_val &= ~EMMC_DLL_RESET_RELEASE;
	npscx_writel(host, SDMMC_RESET_DLL, reg_val);

	reg_val = master_val;
	npscx_writel(host, SDMMC_DLL_MASTER_CTRL, reg_val);

	reg_val = npscx_readl(host, SDMMC_RESET_DLL);
	reg_val |= EMMC_DLL_RESET_RELEASE;
	npscx_writel(host, SDMMC_RESET_DLL, reg_val);

	while (npscx_readl(host, SDMMC_DLL_LOCK) != SDMMC_DLL_LOCKED && delay_count) {
		mdelay(1);
		delay_count--;
	}
	if (!delay_count) {
		pr_err("%s:the dll config failed\n", __func__);
		return -1;
	}

	reg_val = npscx_readl(host, SDMMC_RSYNC_DLL);
	reg_val |= SDMMC_RSYNC_DLLL_EN;
	npscx_writel(host, SDMMC_RSYNC_DLL, reg_val);

	reg_val = npscx_readl(host, SDMMC_RSYNC_DLL);
	reg_val &= ~SDMMC_RSYNC_DLLL_EN;
	npscx_writel(host, SDMMC_RSYNC_DLL, reg_val);

	reg_val = data_stb;
	npscx_writel(host, SDMMC_STB_PHS_CTRL, reg_val);

	reg_val = npscx_readl(host, UHS_REG_EXT);
	reg_val = reg_val & ~(CLK_DRV_PHASE_MASK << CLK_DRV_PHASE_SHIF) & ~(CLK_SMPL_PHASE_MASK << CLK_SMPL_PHASE_SHIF) | (clk_sample << CLK_SMPL_PHASE_SHIF) | (clk_drv << CLK_DRV_PHASE_SHIF);
	npscx_writel(host, UHS_REG_EXT, reg_val);

	reg_val = npscx_readl(host, SDMMC_RSYNC_DLL);
	reg_val |= SDMMC_RSYNC_DLLL_EN;
	npscx_writel(host, SDMMC_RSYNC_DLL, reg_val);

	reg_val = npscx_readl(host, SDMMC_RSYNC_DLL);
	reg_val &= ~SDMMC_RSYNC_DLLL_EN;
	npscx_writel(host, SDMMC_RSYNC_DLL, reg_val);

	reg_val = readl(host->sd_clk_ctrl_vaddr);
	reg_val |= EMMC_DLL_OUT_EN;
	writel(reg_val, host->sd_clk_ctrl_vaddr);
}


static void __npscxmci_set_ios(struct mmc_host *mmc,struct mmc_ios *ios)
{
	u32 div;
	u32 tmp;
	struct npscxmci_host *host = mmc_priv(mmc);
	switch(ios->bus_mode){
		case MMC_BUSMODE_OPENDRAIN:
			/**
			 *Enable open drain pullup for device identification mode
			 */
			//npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_EOD_PUP,CTRL_EOD_PUP);
			break;
		case MMC_BUSMODE_PUSHPULL:
			/**
			 *Disable open drain pullup for device data transfer mode
			 */
			//npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_EOD_PUP,0);
			break;
		default:
			break;
	}

	switch(ios->bus_width){
		case MMC_BUS_WIDTH_1:
			npscxmci_clear_set(host,NPSCXMCI_CTYPE,(CTYPE_8BIT | CTYPE_4BIT),0);
			break;
		case MMC_BUS_WIDTH_4:
			npscxmci_clear_set(host,NPSCXMCI_CTYPE,(CTYPE_8BIT | CTYPE_4BIT),CTYPE_4BIT);
			break;
		case MMC_BUS_WIDTH_8:
			npscxmci_clear_set(host,NPSCXMCI_CTYPE,(CTYPE_8BIT | CTYPE_4BIT),CTYPE_8BIT);
			break;
		default:
			break;
	}

	switch(ios->timing){
		case  MMC_TIMING_LEGACY:
		case  MMC_TIMING_MMC_HS:
		case  MMC_TIMING_SD_HS:
				/**
			 *Nothing to do
			 */
			//npscxmci_debug(host,dinfo,"%s : nothing to do for timing(%x)\n",__func__,ios->timing);
			break;
		case  MMC_TIMING_UHS_SDR12:
		case  MMC_TIMING_UHS_SDR25:
		case  MMC_TIMING_UHS_SDR50:
		case  MMC_TIMING_UHS_SDR104:
			host->r_thold_use = true;
			host->w_thold_use = false;
			break;
		case MMC_TIMING_UHS_DDR50:
			/**
			 * Enable ddr mode
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_DDR_REG,UHS_DDR_REG);
			/**
			 *Disable HS400 ,Half_start = 0.
			 */
			npscxmci_clear_set(host,NPSCXMCI_EMMC_DDR_REG,(EMMC_DDR_HS400 | EMMC_DDR_HSTART),0);
			host->r_thold_use = true;
			host->w_thold_use = false;
			break;
#if 1
		case MMC_TIMING_MMC_HS400:
			/**
			 * Disable ddr mode in uhs reg register
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_DDR_REG,0);
			/**
			 *Enable HS400 ,Half_start = 0.
			 */
			npscxmci_clear_set(host,NPSCXMCI_EMMC_DDR_REG,(EMMC_DDR_HS400 | EMMC_DDR_HSTART),EMMC_DDR_HS400);
			host->r_thold_use = true;
			host->w_thold_use = true;
			set_dll_config(host, SDMMC_DLL_MASTER_CTRL_VAL, 0x30, 0x00, 0x1f);
			break;
#endif
		case  MMC_TIMING_MMC_HS200:
			/**
			 * Disable ddr mode in uhs reg register
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_DDR_REG,0);
			/**
			 *Disable HS400 ,Half_start = 1.
			 */
			npscxmci_clear_set(host,NPSCXMCI_EMMC_DDR_REG,(EMMC_DDR_HS400 | EMMC_DDR_HSTART),EMMC_DDR_HSTART);
			host->r_thold_use = true;
			host->w_thold_use = false;
			set_dll_config(host, SDMMC_DLL_MASTER_CTRL_VAL, 0x1f, 0x00, 0x1f);
			break;
		default:
			break;

	}
	/**
	 * only one card ,so we can write 0 to clkena register.
	 * Diable clock.
	 */
	npscxmci_clear_set(host,NPSCXMCI_CLKENA,-1,0);
	npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
	if(ios->clock > 0){
		if(ios->clock >= host->clk_src)
			div = 0;
		else{
			tmp = 2 * ios->clock;
			div = DIV_ROUND_UP(host->clk_src,tmp);
		}
		//npscxmci_clear_set(host,NPSCXMCI_CLKSRC,);
		if(div > 0xff){
			npscxmci_debug(host,dwarn,"%s the divider is too big\n",__func__);
		}
		div &= 0xff;
		npscxmci_clear_set(host,NPSCXMCI_CLKDIV,0xff,div);
		npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
		/**
		 * Enable the clock,if the card is not sdio ,we can enable low power.
		 */
		if(!npscx_card_sdio(host->prv_data->type))
			npscxmci_clear_set(host,NPSCXMCI_CLKENA,(CLKENA_ENABLE0 | CLKENA_LPOWER0),(CLKENA_ENABLE0 | CLKENA_LPOWER0));
		else
			npscxmci_clear_set(host,NPSCXMCI_CLKENA,(CLKENA_ENABLE0 | CLKENA_LPOWER0 ),(CLKENA_ENABLE0));
		npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);
	}

	switch(ios->power_mode){
		case MMC_POWER_UP:
			npscxmci_clear_set(host,NPSCXMCI_PWREN,PWREN_ENABLE,PWREN_ENABLE);
			/**
			 *for Set command register [send_initialization] bit.
			 */
			NPSCXMCI_SET_FLAGS(host,FLAGS_SND_INIT);
			if(host->vcmmc)
				mmc_regulator_set_ocr(host->mmc, host->vcmmc,ios->vdd);
			break;
		case MMC_POWER_ON:
			break;
		case MMC_POWER_OFF:
			npscxmci_clear_set(host,NPSCXMCI_PWREN,PWREN_ENABLE,0);
			if(host->vcmmc)
				mmc_regulator_set_ocr(host->mmc, host->vcmmc,ios->vdd);
			break;
		default:
			break;
	}


}

static void npscxmci_set_command_parameters(struct npscxmci_host *host,unsigned int *param,struct mmc_command *cmd)
{
	u32 tmp = 0;
	struct mmc_data *data;
	BUG_ON(!cmd);
	data = cmd->data;
	tmp = CMD_START |CMD_USE_HOLD_REG |(cmd->opcode & 0x3f) ;
	if(cmd->flags & MMC_RSP_PRESENT)
		tmp |= CMD_RSP_EXP;
	if(cmd->flags & MMC_RSP_136)
		tmp |= CMD_RSP_LONG;

	if(cmd->flags & MMC_RSP_CRC)
		tmp |= CMD_CHECK_CRC;

	if(!data){

		if(NPSCXMCI_TEST_AND_CLEAR_FLAGS(host,FLAGS_SND_INIT))
			tmp |= CMD_SEND_INIT;

		/**
		 * Stop or abort cmd : cmd12 or cmd52(sdio).
		 */
		if((cmd->opcode == MMC_STOP_TRANSMISSION ) || ((cmd->opcode == SD_IO_RW_DIRECT) && ((((cmd->arg >> 28) & 0x7) == SDIO_CCCR_CCCR)  && (((cmd->arg >> 9) & 0x1ffff) == SDIO_CCCR_ABORT) && ((cmd->arg & 0x07) != 0 ))) )
			tmp |= CMD_STOP_ABORT;
		else
			tmp |= CMD_WAIT_PRVDATA;

		if(cmd->opcode == SD_SWITCH_VOLTAGE){
			tmp |= CMD_VOLT_SWITCH;
			NPSCXMCI_SET_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW);
		}


	}else{
		if(host->mrq->stop  && !host->mrq->sbc){

			if(!host->caps.auto_stop_cmd)
				host->soft_stop_cmd  = true;
			else{
				host->soft_stop_cmd = false;
				tmp |= CMD_AUTO_STOP;
			}
		}else
			host->soft_stop_cmd = false;

		if(data->flags & MMC_DATA_WRITE)
			tmp |= CMD_RW(1);
		tmp |= CMD_DATA_EXP;
		tmp |= CMD_WAIT_PRVDATA;

	}
	//npscxmci_debug(host,derror,"%s : command register value = 0x%08x\n",__func__,tmp);
	*param = tmp;

}


static void npscxmci_request_done(struct npscxmci_host *host)
{
	struct mmc_request *mrq;
	mrq = host->mrq;

	if(!mrq)
		return;

	del_timer(&host->monitor_timer);

	host->mrq = NULL;
	host->data = NULL;
	host->cmd = NULL;

	host->archives_events = 0;
	host->abort_data = NULL;
	host->reset_fsm = NULL;
	host->prepare_data = NULL;
	host->cleanup = NULL;

	host->cmd_archives = 0;
	host->data_archives = 0;

	mmc_request_done(host->mmc,mrq);
}

static void npscxmci_complete_command(struct npscxmci_host *host,struct mmc_command *cmd)
{
	unsigned cmd_archives = host->cmd_archives;

	if(cmd->flags & MMC_RSP_PRESENT){
		if(cmd->flags & MMC_RSP_136){
			cmd->resp[3] = npscx_readl(host,RESP0);
			cmd->resp[2] = npscx_readl(host,RESP1);
			cmd->resp[1] = npscx_readl(host,RESP2);
			cmd->resp[0] = npscx_readl(host,RESP3);
		}else{
			cmd->resp[0] = npscx_readl(host,RESP0);
		}
	}
	if(cmd_archives & MINTSTS_RTO) {
		cmd->error = -ETIMEDOUT;
		udelay(100);
	} else if(cmd_archives & (MINTSTS_RCRC | MINTSTS_EBE |MINTSTS_RE )){
		cmd->error = -EILSEQ;
		npscxmci_debug(host,derror,"cmd%d response crc endbit error\n",cmd->opcode);
	} else if(cmd_archives & MINTSTS_HLE) {
		cmd->error = -EILSEQ;
		npscxmci_debug(host,derror,"cmd%d hardware locked write error\n",cmd->opcode);
	} else
		cmd->error = 0;

}


static void npscxmci_monitor_timer(unsigned long data)
{
	struct npscxmci_host *host = (struct npscxmci_host *)data;
	struct mmc_host *mmc = host->mmc;
	unsigned int state;
	struct mmc_command *cmd;
	spin_lock_bh(&host->lock);
	state = host->state;

	npscxmci_debug(host,derror,"[%s]soft monitor timeout \n",mmc_hostname(mmc));
	npscxmci_dump_registers(host);
	if(host->mrq){
		switch(state){
			case	STATE_IDLE:
				break;
			case	STATE_WAITING_RSP:
				host->cmd->error = -ETIMEDOUT;
				break;
			case	STATE_XFER_DATA:
				host->data->error = -ETIMEDOUT;
				break;
			case	STATE_WAIT_BUSYDONE:
				cmd = host->cmd;
				if(cmd->data)
					cmd->data->error = -ETIMEDOUT;
				else
					cmd->error = -ETIMEDOUT;
				break;
			case	STATE_WAITING_STOP_RSP:
				host->cmd->error = -ETIMEDOUT;
				break;
			case	STATE_REQUEST_DONE:
				break;


		}
		if(state != STATE_IDLE){
			if(host->abort_data)
				host->abort_data(host,host->cmd);
			if(host->reset_fsm)
				host->reset_fsm(host);
			if(host->cleanup)
				host->cleanup(host);
			npscxmci_request_done(host);
			host->state = STATE_IDLE;
		}

	}
	spin_unlock_bh(&host->lock);
}
static int inline npscxmci_request_adjust(struct npscxmci_host *host ,struct mmc_data *data)
{

	struct scatterlist *sg;
	int i;
	BUG_ON(host->data);

	host->data = data;


	/*
	 * if enable card read threshold,blocksize must allign dword
	 */
	if(likely(!(data->blksz % 4)))
		host->blksz_allign_dword = true;
	else
		host->blksz_allign_dword = false;

	/*
	 *if the bus width is 32bit and use DMA,the  buffer size must allign dword and addr must allign dword.
	 */
	for_each_sg(data->sg,sg,data->sg_len,i){
		if(unlikely((sg->offset & (DMA_DESC_ADDR_ALIGN - 1)) || (sg->length & (DMA_DESC_ADDR_ALIGN -1 ))))
			return -EINVAL;
	}
	return 0;



}

static void npscxmci_start_request(struct npscxmci_host *host,struct mmc_command *cmd)
{

	struct mmc_data *data;
	unsigned int blksz;
	unsigned int blocks;
	unsigned int param;
	data = cmd->data;
	host->cmd = cmd;
	if(data){
		if(npscxmci_request_adjust(host,data)){
			/*
			 * PIO transfer
			 */
			host->prepare_data = npscxmci_prepare_pio;
			host->start_data = npscxmci_start_pio;
			host->abort_data = npscxmci_stop_pio;
			host->cleanup = npscxmci_pio_cleanup;
			host->reset_fsm = npscxmci_piomode_reset_fsm;
			NPSCXMCI_CLREAR_FLAGS(host,FLAGS_USE_DMA_TRAN);
			npscxmci_debug(host,dinfo,"command%d width data using PIO\n",cmd->opcode);
		}else{
			/**
			 *DMA transfer
			 */
			host->prepare_data = npscxmci_prepare_dma;
			host->start_data = npscxmci_start_dma;
			host->abort_data = npscxmci_stop_dma;
			host->cleanup = npscxmci_dma_cleanup;
			host->reset_fsm = npscxmci_dmamode_reset_fsm;
			NPSCXMCI_SET_FLAGS(host,FLAGS_USE_DMA_TRAN);
			npscxmci_debug(host,dinfo,"command%d width data using DMA\n",cmd->opcode);
		}

		host->prepare_data(host,cmd);
		host->start_data(host,cmd);
		blocks = data->blocks;
		blksz = data->blksz;
		blocks *= blksz;
		npscxmci_debug(host,ddebug,"%s : bycnt = %d,blksz = %d\n",__func__,blocks,blksz);
		npscx_writel(host,BLKSIZ,(blksz & 0xffff));
		npscx_writel(host,BYTCNT,blocks);
	}else
		host->reset_fsm = npscxmci_command_reset_fsm;//npscxmci_reset_fsm;

	npscxmci_set_command_parameters(host,&param,cmd);
	if(data || (cmd->flags & MMC_RSP_BUSY)){
		npscxmci_config_timeout(host,cmd);
		if(data && (data->flags & MMC_DATA_WRITE)){
			npscxmci_clear_set(host,NPSCXMCI_INTMASK,MINTSTS_BCI,MINTSTS_BCI);
			npscxmci_clear_set(host,NPSCXMCI_CARDTHRCTL,CARDTHRCTL_BCIEN,CARDTHRCTL_BCIEN);
		}
		else  if(data && (data->flags & MMC_DATA_READ)){
			npscxmci_clear_set(host,NPSCXMCI_INTMASK,MINTSTS_BCI,MINTSTS_BCI);
			npscxmci_clear_set(host,NPSCXMCI_CARDTHRCTL,CARDTHRCTL_BCIEN,0);
		}
		else{
			npscxmci_clear_set(host,NPSCXMCI_INTMASK,MINTSTS_BCI,0);
			npscxmci_clear_set(host,NPSCXMCI_CARDTHRCTL,CARDTHRCTL_BCIEN,0);
		}
	}
	if(data)
		npscxmci_config_threshold_fifo(host);
	//npscxmci_set_command_parameters(host,&param,cmd);
	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		host->state = STATE_WAITING_STOP_RSP;
	else
		host->state = STATE_WAITING_RSP;

	mod_timer(&host->monitor_timer, jiffies + 30 * HZ);
	npscx_writel(host,CMDARG,cmd->arg);
	wmb();
	npscx_writel(host,CMD,param);
}

static int __npscxmci_get_cd(struct npscxmci_host *host)
{

	struct npscxmci_pltfm_data *private = host->prv_data;
	///BUG_ON();
	if(npscx_card_removal(private->type)){
		if(gpio_is_valid(private->cd_pin.gpio))
			return (private->cd_pin.active_low ^ gpio_get_value(private->cd_pin.gpio));
		/**
		 * cdetect register value:
		 * 0 represents presence of card.
		 * 1 represents card is not present.
		 */
		return !(npscx_readl(host,CDETECT) & CDETECT_CARD_REMOVE);
	}else if(npscx_card_softplug(private->type)){

		smp_rmb();
		if(NPSCXMCI_TEST_FLAGS(host,FLAGS_CARD_PRESENT))
			return 1;
		else
			return 0;
	}else
		return 1;
}

static void npscxmci_detect_card_tasklet(unsigned long data)
{
	struct npscxmci_host *host = (struct npscxmci_host *)data;
	struct mmc_command *cmd;
	int present;
	int old_preset;
	u32 state;
	spin_lock(&host->lock);
	present = __npscxmci_get_cd(host);
	smp_mb();
	old_preset = NPSCXMCI_TEST_AND_CLEAR_FLAGS(host,FLAGS_CARD_PRESENT);
	smp_mb();
	state = host->state;
	if((present != old_preset) && !present){
		npscxmci_debug(host,ddebug,"%s : card is removed\n",__func__);
		if(host->mrq){
			switch(state){
				case	STATE_IDLE:
					break;

				case	STATE_WAITING_RSP:
					host->cmd->error = -ENOMEDIUM;
					break;

				case	STATE_XFER_DATA:
					host->data->error = -ENOMEDIUM;
					break;
				case	STATE_WAITING_STOP_RSP:
					host->cmd->error = -ENOMEDIUM;
					break;
				case	STATE_WAIT_BUSYDONE:
					cmd = host->cmd;
					if(cmd->data)
						cmd->data->error = -ENOMEDIUM;
					else
						cmd->error = -ENOMEDIUM;
					break;
				case	STATE_REQUEST_DONE:
					break;


			}
			if(host->state != STATE_IDLE){
				//host->clearup(host);
				if(host->abort_data)
					host->abort_data(host,host->cmd);
				if(host->reset_fsm)
					host->reset_fsm(host);
				if(host->cleanup)
					host->cleanup(host);
				npscxmci_request_done(host);

				host->state = STATE_IDLE;
			}
			/**
			 * if the card removed before calling the npscxmci_start_request for new request.
			 * we must
			 *
			 */
			//npscxmci_request_done(host);
		//host->reset_fsm(host);
		}


	}else{
		NPSCXMCI_SET_FLAGS(host,FLAGS_CARD_PRESENT);
		npscxmci_debug(host,ddebug,"%s : card is insert\n",__func__);
	}
	spin_unlock(&host->lock);
	if(gpio_is_valid(host->prv_data->cd_pin.gpio)){
		//enable_irq(host->irq);
		enable_irq(host->prv_data->irq_cd);
	}
	mmc_detect_change(host->mmc,msecs_to_jiffies(500));


}

static void npscxmci_state_machine_tasklet(unsigned long d)
{

	struct npscxmci_host *host = (struct npscxmci_host *)d;

	u32 state,prv_state;
	u32 data_archives;
	struct mmc_command *cmd;
	struct mmc_data *data;
	state  = host->state;
	spin_lock(&host->lock);
	do{
		prv_state = state;
		switch(state){
			case	STATE_IDLE:
				break;

			case	STATE_WAITING_RSP:
				if(!NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_COMMAND_DONE))
					break;

				NPSCXMCI_SET_ACCOMPLISHED(host,EVENT_COMMAND_DONE);
				WARN_ON(!host->cmd);
				cmd = host->cmd;

				smp_rmb();
				npscxmci_complete_command(host,cmd);


				if(cmd->error){
					if(host->abort_data)
						host->abort_data(host,cmd);
					if(host->reset_fsm)
						host->reset_fsm(host);
					state = STATE_REQUEST_DONE;
					break;
				}
				if(cmd == host->mrq->sbc){
					npscxmci_start_request(host,host->mrq->cmd);
					state = STATE_WAITING_RSP;
					break;
				}
				/*if(!cmd->data && (cmd->flags & MMC_RSP_BUSY)){
					state = STATE_WAIT_BUSYDONE;
					break;
				}else*/
				if(cmd->data){
					state = STATE_XFER_DATA;
					break;
				}else{
					state = STATE_REQUEST_DONE;
					break;
				}
				break;

			case	STATE_XFER_DATA:

				BUG_ON(!host->data);
				if(NPSCXMCI_TEST_ARCHIVES(host,EVENT_DATA_ERROR)){
					npscxmci_debug(host,derror,"cmd%d data error happens\n",host->cmd->opcode);
					state = STATE_DATA_ERROR;
					break;
				}
				if(!NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_DATA_DONE))
					break;
				NPSCXMCI_SET_ACCOMPLISHED(host,EVENT_DATA_DONE);
				if(host->soft_stop_cmd && (host->data->flags & MMC_DATA_READ)){
					npscxmci_start_request(host,host->mrq->stop);
					state = STATE_WAITING_STOP_RSP;
					break;
				}else if((host->data->flags & MMC_DATA_WRITE)){
					state = STATE_WAIT_BUSYDONE;
					break;
				}else{
					state = STATE_REQUEST_DONE;
					break;
				}



			case	STATE_DATA_ERROR:
				BUG_ON(!host->data);
				if(!NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_DATA_ERROR))
					break;
				NPSCXMCI_SET_ACCOMPLISHED(host,EVENT_DATA_ERROR);

				npscxmci_debug(host,derror,"cmd%d data error ,and mintsts = 0x%08x\n",host->cmd->opcode,host->data_archives);
				npscxmci_dump_registers(host);
				host->abort_data(host,host->cmd);
				/*
				 *Here maybe should reset host firstly
				 */
				host->reset_fsm(host);
				if(host->mrq->stop){
					npscxmci_start_request(host,host->mrq->stop);
					return;
				}else{
					state = STATE_REQUEST_DONE;
					break;
				}

			case	STATE_WAITING_STOP_RSP:
				BUG_ON(!host->cmd);
				if(!NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_COMMAND_DONE))
					break;

				NPSCXMCI_SET_ACCOMPLISHED(host,EVENT_COMMAND_DONE);
				//cmd_archives = host->cmd_archives;
				cmd = host->cmd;

				npscxmci_complete_command(host,cmd);

				if(cmd->error){
					npscxmci_dump_registers(host);
					if(host->abort_data)
						host->abort_data(host,cmd);
					if(host->reset_fsm)
						host->reset_fsm(host);
					//state = STATE_REQUEST_DONE;
					//break;
				}

				state = STATE_REQUEST_DONE;
				break;

			case	STATE_WAIT_BUSYDONE:
				/**
				 * busy signal done may be timeout.
				 */
				/*
				if(NPSCXMCI_TEST_ARCHIVES(EVENT_DATA_ERROR,host)){
					state = STATE_DATA_ERROR;
					break;
				}
				*/
				if(!NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_BUSY_DONE))
					break;
				NPSCXMCI_SET_ACCOMPLISHED(host,EVENT_BUSY_DONE);
				/*
				if(!host->data && (host->cmd & MMC_RSP_BUSY) ){
					state = STATE_REQUEST_DONE;
					break;
				}
				if(host->data && (host->data->flags & MMC_DATA_WRITE )){ //after auto stop command  completed
					state = STATE_REQUEST_DONE;
					break;
				}
				*/
				cmd = host->cmd;
				if(cmd->data && (cmd->data->flags & MMC_DATA_WRITE)){
					if(host->soft_stop_cmd){
						npscxmci_start_request(host,host->mrq->stop);
						state = STATE_WAITING_STOP_RSP;
					}else
						state = STATE_REQUEST_DONE;
				}else{
					state = STATE_REQUEST_DONE;
				}

				break;


			case	STATE_REQUEST_DONE:
				BUG_ON(!host->mrq);
				smp_rmb();
				if(host->mrq->data){
					data_archives = host->data_archives;
					data = host->mrq->data;
					if(data_archives){
					/*
					 *
					 * TO DO Error handler.
					 */
						if(data->flags & MMC_DATA_READ){
							if(data_archives & MINTSTS_DRTO )
								data->error = -ETIMEDOUT;
							else if(data_archives & (MINTSTS_EBE | MINTSTS_BCI | MINTSTS_FRUN | MINTSTS_DCRC))
								data->error = -EILSEQ;
							else
								data->error = -EIO;

						}else{
							if(data_archives & MINTSTS_EBE) {
								host->mrq->cmd->error = -ETIMEDOUT;
								data->error = -ETIMEDOUT;
							}
							else if(data_archives & (MINTSTS_FRUN | MINTSTS_DCRC)) {
								host->mrq->cmd->error = -EILSEQ;
								data->error = -EILSEQ;
							}
							else {
								host->mrq->cmd->error = -EIO;
								data->error = -EIO;
							}
						}
						host->mrq->data->bytes_xfered = 0;
					}else{
						host->mrq->data->error = 0;
						host->mrq->data->bytes_xfered = data->blocks * data->blksz;
					}
				}
				if(host->cleanup)
					host->cleanup(host);
				npscxmci_request_done(host);
				state = STATE_IDLE;

				break;
		}


	}while(state != prv_state);
	host->state = state;
	spin_unlock(&host->lock);
	return;
}


static irqreturn_t npscxmci_irq(int irq,void *devid)
{

	struct npscxmci_host *host = (struct npscxmci_host *)devid;

	int round_cycle = 10;
	u32 mintsts = 0;
	int result;
	struct mmc_data *data ;//= host->data;

	struct mmc_command *cmd;

	do{

		//data = host->data;

		mintsts = npscx_readl(host,MINTSTS);
		//if(host->cmd)
		//npscxmci_debug(host,derror,"cmd%d enter interrupt = %x\n",host->cmd->opcode,mintsts);
		//else
		//npscxmci_debug(host,derror,"enter interrupt= %x\n",mintsts);
		if(!mintsts ){
			if(round_cycle == 10){
				npscxmci_debug(host,dwarn,"%s : no interrupts happen or interrupts have handled in last interrupt handler\n",__func__);
				return IRQ_NONE;
			}
			break;
		}
		if(mintsts & CARD_DETECT_MASK){
			npscx_writel(host,RINTSTS,mintsts & CARD_DETECT_MASK);
			tasklet_schedule(&host->cd_tasklet);
			break;
		}
		if(NPSCXMCI_TEST_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW) || NPSCXMCI_TEST_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_HIGH)){
			if(mintsts & VLOT_SWITCH_MASK){
				npscx_writel(host,RINTSTS,mintsts & VLOT_SWITCH_MASK);
				mintsts &= ~VLOT_SWITCH_MASK;
				smp_wmb();
				if(NPSCXMCI_TEST_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW)){
					NPSCXMCI_SET_ARCHIVES(host,EVENT_COMMAND_DONE);
					tasklet_schedule(&host->state_tasklet);
				}

				break;
			}
			if(mintsts & CMD_DONE_MASK){
				npscx_writel(host,RINTSTS,mintsts & CMD_DONE_MASK);
				mintsts &= ~CMD_DONE_MASK;
			}
		}

		if(mintsts & CMD_DONE_MASK){
			npscx_writel(host,RINTSTS,mintsts & CMD_DONE_MASK);
			//host->cmd_archives = mintsts
			smp_wmb();
			NPSCXMCI_SET_ARCHIVES(host,EVENT_COMMAND_DONE);
			//smp_wmb();
			tasklet_schedule(&host->state_tasklet);

		}

		if(mintsts & CMD_ERROR_MASK){
			npscx_writel(host,RINTSTS,mintsts & CMD_ERROR_MASK);
			host->cmd_archives |= mintsts & CMD_ERROR_MASK;
			smp_wmb();
			NPSCXMCI_SET_ARCHIVES(host,EVENT_COMMAND_DONE);

			tasklet_schedule(&host->state_tasklet);
			break;

		}
		/*
		 *host->data maybe changes NULL ,if the state_tasklet runs in the other cpu.
		 */

		data = host->data;
		if(data){

			if(data->flags & MMC_DATA_WRITE){
				if(mintsts & DATA_WRITE_ERROR_MASK){
					npscx_writel(host,RINTSTS,mintsts & DATA_WRITE_ERROR_MASK | MINTSTS_DTO | MINTSTS_BCI);
					host->data_archives |= mintsts & DATA_WRITE_ERROR_MASK;
					smp_wmb();
					NPSCXMCI_SET_ARCHIVES(host,EVENT_DATA_ERROR);
					tasklet_schedule(&host->state_tasklet);
					break;
				}
			}else{
				if(mintsts & DATA_READ_ERROR_MASK){
					npscx_writel(host,RINTSTS,mintsts & DATA_READ_ERROR_MASK | MINTSTS_DTO);
					host->data_archives |= mintsts & DATA_READ_ERROR_MASK;
					smp_wmb();

					NPSCXMCI_SET_ARCHIVES(host,EVENT_DATA_ERROR);
					//smp_wmb();
					tasklet_schedule(&host->state_tasklet);

					break;

				}
			}
		}

		if(mintsts & DATA_READ_MASK){
			npscx_writel(host,RINTSTS,mintsts & DATA_READ_MASK);
			smp_wmb();
			result = npscxmci_pio_read(host,false);
			if(result){
				//tasklet_schedule(&host->state_tasklet);
				break;
			}

		}
		if(mintsts & DATA_WRITE_MASK){
			npscx_writel(host,RINTSTS,mintsts & DATA_WRITE_MASK);
			smp_wmb();
			result = npscxmci_pio_write(host);
			if(result){
				//tasklet_schedule(&host->state_tasklet);
				break;
			}
		}

		if(mintsts & DATA_DONE_MASK){
			npscx_writel(host,RINTSTS,mintsts & DATA_DONE_MASK);
			smp_wmb();
			NPSCXMCI_SET_ARCHIVES(host,EVENT_DATA_DONE);
			//smp_wmb();
			if(data && (data->flags & MMC_DATA_READ) && !NPSCXMCI_TEST_FLAGS(host,FLAGS_USE_DMA_TRAN)){
				npscxmci_pio_read(host,true);
			}

			if(data && (data->flags & MMC_DATA_READ)){
				tasklet_schedule(&host->state_tasklet);
			}
		}
		cmd = host->cmd;

		if(cmd && cmd->data && (cmd->data->flags & MMC_DATA_WRITE)){
			if(mintsts  & BUSY_CLEAR_INT_MASK){
				npscx_writel(host,RINTSTS,mintsts & BUSY_CLEAR_INT_MASK);
				smp_wmb();
				NPSCXMCI_SET_ARCHIVES(host,EVENT_BUSY_DONE);
				//smp_wmb();
				tasklet_schedule(&host->state_tasklet);
				break;
			}
		}


		if(mintsts & SDIO_INT_MASK){
			npscx_writel(host,RINTSTS,mintsts & SDIO_INT_MASK);
			mmc_signal_sdio_irq(host->mmc);
		}

		npscxmci_debug(host,ddebug,"%s : after clear rintsts register : 0x%08x\n",__func__,npscx_readl(host,RINTSTS));

	}while(round_cycle-- > 0 );
	return IRQ_HANDLED;
}



static irqreturn_t npscxmci_card_detect_irq(int irq,void *devid)
{
	struct npscxmci_host *host = (struct npscxmci_host *)devid;

	disable_irq_nosync(irq);
	//disable_irq_nosync(host->irq);
	tasklet_schedule(&host->cd_tasklet);

	return IRQ_HANDLED;

}

static int __npscxmci_signal_voltage_switch(struct mmc_host *mmc,struct mmc_ios *ios)
{
	int result;
	struct npscxmci_host *host = mmc_priv(mmc);
	npscxmci_debug(host,dinfo,"%s : io-voltage = %x\n",__func__,ios->signal_voltage);

		/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++
		 |  volt_reg    mmc_volt_reg            decode voltage  |
		 |      0            0                      3.3v        |
		 |      0            1                      3.3v        |
		 |      1            0                      1.8v        |
		 |	1            1                      1.2v        |
		 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
	switch(ios->signal_voltage){
		case MMC_SIGNAL_VOLTAGE_330:
			/*
			 * clear VOLT_REG ,
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_REG_1V8,0);
			if(host->vqmmc){
				result = regulator_set_voltage(host->vqmmc,2700000,3600000);
				if(result){
					npscxmci_debug(host,derror,"%s : switch signal_voltage to 3.3v failed\n",__func__);
					return result;
				}
			}
			break;


		case MMC_SIGNAL_VOLTAGE_180:
			/**
			 *set VOLT_REG ,and clear MMC_VOLT_REG
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_REG_1V8,UHS_REG_1V8);
			/**
			 *
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG_EXT,EUHS_MMC_VOLT_1V2,0);
			if(host->vqmmc){
				result = regulator_set_voltage(host->vqmmc,1700000,1950000);
				if(result){
					npscxmci_debug(host,derror,"%s : switch signal_voltage to 1.8v failed\n",__func__);
					return result;
				}
			}
			break;

		case MMC_SIGNAL_VOLTAGE_120:
			/**
			 * Set VOLT_REG and MMC_VOLT_REG
			 */
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG,UHS_REG_1V8,UHS_REG_1V8);
			npscxmci_clear_set(host,NPSCXMCI_UHS_REG_EXT,EUHS_MMC_VOLT_1V2,EUHS_MMC_VOLT_1V2);

			if(host->vqmmc){
				result = regulator_set_voltage(host->vqmmc,1100000,1300000);
				if(result){
					npscxmci_debug(host,derror,"%s : switch signal_voltage to 1.2v failed\n",__func__);
					return result;
				}
			}
			break;
		default:

			npscxmci_debug(host,derror,"%s : can't support the voltage %x",__func__,ios->signal_voltage);
			return -EOPNOTSUPP;

	}
	return 0;
}


static int npscxmci_start_signal_voltage_switch(struct mmc_host *mmc,struct mmc_ios *ios)
{
	int result;
	result = __npscxmci_signal_voltage_switch(mmc,ios);
	return result;
}
static int npscxmci_get_cd(struct mmc_host *mmc)
{
	struct npscxmci_host *host = mmc_priv(mmc);
	return __npscxmci_get_cd(host);
}


static int npscxmci_get_ro(struct mmc_host *mmc)
{

	unsigned int rvalue;
	struct npscxmci_host *host = mmc_priv(mmc);
	struct npscxmci_pltfm_data *private = host->prv_data;
	if(npscx_card_with_wp(private->type)){
		if(gpio_is_valid(private->wp_pin.gpio)){
			return (private->wp_pin.active_low ^ gpio_get_value(private->wp_pin.gpio));
		}else{
			rvalue = npscx_readl(host,WRTPRT);

			return !!(rvalue & WRTPRT_PROTECT);
		}
	}
	return 0;
}

static void npscxmci_set_ios(struct mmc_host *mmc,struct mmc_ios *ios)
{
	__npscxmci_set_ios(mmc,ios);
}

static void npscxmci_enable_sdio_irq(struct mmc_host *mmc,int enable)
{
	u32 tmp;
	struct npscxmci_host *host = mmc_priv(mmc);
	if(enable){
		tmp = npscx_readl(host,CLKENA);
		if(tmp & CLKENA_LPOWER0){
			npscxmci_clear_set(host,NPSCXMCI_CLKENA,CLKENA_LPOWER0,0);
			/*
			 *send updata clock command.
			 */
			npscxmci_config_clk_cmd(host,(CMD_START | CMD_UPCLK_ONLY | CMD_WAIT_PRVDATA),500);

		}
		npscxmci_clear_set(host,NPSCXMCI_INTMASK,INTMASK_SDIO0,INTMASK_SDIO0);
		NPSCXMCI_SET_FLAGS(host,FLAGS_SDIO_IRQ_ENABLED);
	}else{
		/*unmask detect sdio card interrupt*/
		npscxmci_clear_set(host,NPSCXMCI_INTMASK,INTMASK_SDIO0,0);
		NPSCXMCI_CLREAR_FLAGS(host,FLAGS_SDIO_IRQ_ENABLED);
	}

}


static int npscxmci_execute_tuning(struct mmc_host *mmc,u32 command)
{
	int retval;

	struct npscxmci_host *host = mmc_priv(mmc);
	retval = mmc_send_tuning(mmc,command);
	if(retval)
		npscxmci_debug(host,derror,"%s execute tuning failed\n",mmc_hostname(mmc));
	else
		npscxmci_debug(host,derror,"%s execute tuning sucess\n",mmc_hostname(mmc));
	return retval;
}


static void npscxmci_request(struct mmc_host *mmc,struct mmc_request *mrq)
{
	struct npscxmci_host *host = mmc_priv(mmc);
	int present;
	spin_lock_bh(&host->lock);
	BUG_ON(host->mrq);
	host->mrq = mrq;
	present = __npscxmci_get_cd(host);
	if(!present){
		mrq->cmd->error = -ENOMEDIUM;
		npscxmci_request_done(host);
		spin_unlock_bh(&host->lock);
		return;
	}
	NPSCXMCI_CLREAR_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW);
	NPSCXMCI_CLREAR_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_HIGH);
	if(mrq->sbc)
		npscxmci_start_request(host,mrq->sbc);
	else
		npscxmci_start_request(host,mrq->cmd);
	spin_unlock_bh(&host->lock);
}
static int npscxmci_card_busy(struct mmc_host *mmc)
{
	struct npscxmci_host *host = (struct npscxmci_host *)mmc_priv(mmc);
	unsigned int rvalue;
	rvalue = npscx_readl(host,STATUS);
#if 0
	if(NPSCXMCI_TEST_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW)){
		/*
		if(NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_VOLT_SWITCH_INT_LOW))
			return 1;
		else if(NPSCXMCI_TEST_AND_CLEAR_ARCHIVES(host,EVENT_VOLT_SWITCH_INT_HIGH)){
			NPSCXMCI_CLREAR_FLAGS(host,FLAGS_DOING_VOLT_SWITCH);
			return 0;
		}
		else
			return 0;
		*/
		if(NPSCXMCI_TEST_AND_CLEAR_ACCOMPLISHED(host,EVENT_VOLT_SWITCH_INT)){

			return !!(rvalue & STATUS_DATA_BUSY);
		}else
			return !(rvalue & STATUS_DATA_BUSY);
	}
#endif
	if(NPSCXMCI_TEST_AND_CLEAR_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_LOW)){
		NPSCXMCI_SET_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_HIGH);
	}else if(NPSCXMCI_TEST_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_HIGH)){
		NPSCXMCI_CLREAR_FLAGS(host,FLAGS_DOING_VOLT_SWITCH_HIGH);
	}

	return !!(rvalue & STATUS_DATA_BUSY);
}

/**
 *npscxmci_soft_hotplug - soft remove or init given device,like wifi module,You can close the power ,clock of wifi
 *module when the device is suspend,This can help reduce power consumption.
 *@card_type :the device type.
 *@remove : remove device or init device.
 */
int npscxmci_soft_hotplug(const unsigned int card_type,int remove)
{

	struct npscxmci_host *hentry = NULL;
	int found = 0;
	if(!list_empty(&npscxmci_softplug_list)){
		list_for_each_entry(hentry,&npscxmci_softplug_list,node){
			if((hentry->prv_data->type  & 0x07) == card_type){
				found = 1;
				break;
			}
		}
	}
	if(found){
		if(!npscx_card_softplug(hentry->prv_data->type)){

			npscxmci_debug(hentry,derror,"It  is not soft hotplug device\n");
			return -EPERM;
		}
		if(remove){
			smp_mb__before_clear_bit();
			NPSCXMCI_CLREAR_FLAGS(hentry,FLAGS_CARD_PRESENT);
			smp_mb__before_clear_bit();
			mmc_detect_change(hentry->mmc,msecs_to_jiffies(200));
			return 0;
		}else{
			NPSCXMCI_SET_FLAGS(hentry,FLAGS_CARD_PRESENT);
			mmc_detect_change(hentry->mmc,msecs_to_jiffies(200));
			return 0;
		}
	}
	return -ENODEV;
}

EXPORT_SYMBOL(npscxmci_soft_hotplug);
/**
 *Debug functions
 *
 *npscxmci_dump_registers@TODO :we can dump some registers when error happens in data transfering.
 *
 *npscxmci_registers_ops@TODO : we can read some registers through debugfs any times.
 *
 *npscxmci_state_ops@TODO : we can know the host states through debugfs any times.
 *
 */

static void npscxmci_dump_registers(struct npscxmci_host *host)
{
	u32 value;
	npscxmci_debug(host,derror,"+-------------------+DUMP REGISTER+----------------+\n");
	npscxmci_debug(host,derror,"|\tCTRL:       0x%08x|\tCLKDIV:     0x%08x|\n",	\
			npscx_readl(host,CTRL),npscx_readl(host,CLKDIV));
	npscxmci_debug(host,derror,"|\tCLKENA:     0x%08x|\tCTYPE:      0x%08x|\n",	\
			npscx_readl(host,CLKENA),npscx_readl(host,CTYPE));
	npscxmci_debug(host,derror,"|\tBLKSIZ:     0x%08x|\tBYTCNT:     0x%08x|\n",	\
			npscx_readl(host,BLKSIZ),npscx_readl(host,BYTCNT));
	npscxmci_debug(host,derror,"|\tINTMASK:    0x%08x|\tCMDARG:     0x%08x|\n",	\
			npscx_readl(host,INTMASK),npscx_readl(host,CMDARG));
	npscxmci_debug(host,derror,"|\tCMD:        0x%08x|\tMINTSTS:    0x%08x|\n",	\
			npscx_readl(host,CMD),npscx_readl(host,MINTSTS));
	npscxmci_debug(host,derror,"|\tRINTSTS:    0x%08x|\tSTATUS:     0x%08x|\n",	\
			npscx_readl(host,RINTSTS),npscx_readl(host,STATUS));
	npscxmci_debug(host,derror,"|\tFIFOTH:     0x%08x|\tCDETECT:    0x%08x|\n",	\
			npscx_readl(host,FIFOTH),npscx_readl(host,CDETECT));
	npscxmci_debug(host,derror,"|\tWRTPRT:     0x%08x|\tTCBCNT:     0x%08x|\n",	\
			npscx_readl(host,WRTPRT),npscx_readl(host,TCBCNT));
	npscxmci_debug(host,derror,"|\tTBBCNT:     0x%08x|\tHCON:       0x%08x|\n",	\
			npscx_readl(host,TBBCNT),npscx_readl(host,HCON));
	npscxmci_debug(host,derror,"|\tUHSREG:     0x%08x|\tBMOD:       0x%08x|\n",	\
			npscx_readl(host,UHS_REG),npscx_readl(host,BMOD));
	npscxmci_debug(host,derror,"|\tDBADDR:     0x%08x|\tDSCADDR:    0x%08x|\n",	\
			npscx_readl(host,DBADDR),npscx_readl(host,DSCADDR));
	npscxmci_debug(host,derror,"|\tIDSTS:      0x%08x|\tIDINTEN:    0x%08x|\n",	\
			npscx_readl(host,IDSTS),npscx_readl(host,IDINTEN));
	npscxmci_debug(host,derror,"|\tBUFADDR:    0x%08x|\tCARDTHCTL:  0x%08x|\n",	\
			npscx_readl(host,BUFADDR),npscx_readl(host,CARDTHRCTL));
	npscxmci_debug(host,derror,"|\tUHSREGEXT:  0x%08x|\tEMMCDDRREG: 0x%08x|\n",	\
			npscx_readl(host,UHS_REG_EXT),npscx_readl(host,EMMC_DDR_REG));
	npscxmci_debug(host,derror,"|\tENABLESHIFT:0x%08x|\tUSRID:      0x%08x|\n",	\
			npscx_readl(host,ENABLE_SHIFT),npscx_readl(host,USRID));
	npscxmci_debug(host,derror,"+--------------------------------------------------+\n");
	value = npscx_readl(host,STATUS);
	value = (value >> 4) & 0xf;
	if(value < 16)
		npscxmci_debug(host,derror,"|CMD FSM STATUS:%s|\n",npscxmci_cmd_fsm_status[value]);
	value = npscx_readl(host,IDSTS);
	value = (value >> 13) & 0xf;
	if(value < 9)
		npscxmci_debug(host,derror,"|DMA FSM STATUS:%s|\n",npscxmci_dmac_fsm_status[value]);
	//debug
	if(NPSCXMCI_TEST_FLAGS(host,FLAGS_USE_DMA_TRAN) && host->data)
		npscxmci_debug(host,derror,"using DMA transfer\n");
	else if(host->data)
		npscxmci_debug(host,derror,"using PIO transfer\n");
}
#ifdef	CONFIG_DEBUG_FS
static int npscxmci_registers_show(struct seq_file *sf,void *data)
{
	struct npscxmci_host *host = sf->private;
	u32	value;
	/*
	 *TODO : pmruntime get
	 */
	seq_printf(sf,"\t\tCTRL:	 0x%08x\n",npscx_readl(host,CTRL));
	seq_printf(sf,"\t\tCLKDIV:       0x%08x\n",npscx_readl(host,CLKDIV));
	seq_printf(sf,"\t\tCLKENA:       0x%08x\n",npscx_readl(host,CLKENA));
	seq_printf(sf,"\t\tCTYPE:        0x%08x\n",npscx_readl(host,CTYPE));
	seq_printf(sf,"\t\tBLKSIZ:       0x%08x\n",npscx_readl(host,BLKSIZ));
	seq_printf(sf,"\t\tBYTCNT:      0x%08x\n",npscx_readl(host,BYTCNT));
	seq_printf(sf,"\t\tINTMASK:      0x%08x\n",npscx_readl(host,INTMASK));
	seq_printf(sf,"\t\tCMDARG:       0x%08x\n",npscx_readl(host,CMDARG));
	seq_printf(sf,"\t\tCMD:          0x%08x\n",npscx_readl(host,CMD));
	seq_printf(sf,"\t\tMINTSTS:      0x%08x\n",npscx_readl(host,MINTSTS));
	seq_printf(sf,"\t\tRINTSTS:      0x%08x\n",npscx_readl(host,RINTSTS));
	seq_printf(sf,"\t\tSTATUS:       0x%08x\n",npscx_readl(host,STATUS));
	seq_printf(sf,"\t\tFIFOTH:       0x%08x\n",npscx_readl(host,FIFOTH));
	seq_printf(sf,"\t\tCDECT:        0x%08x\n",npscx_readl(host,CDETECT));
	seq_printf(sf,"\t\tWRTPRT:       0x%08x\n",npscx_readl(host,WRTPRT));
	seq_printf(sf,"\t\tTCBCNT:       0x%08x\n",npscx_readl(host,TCBCNT));
	seq_printf(sf,"\t\tTBBCNT:       0x%08x\n",npscx_readl(host,TBBCNT));
	seq_printf(sf,"\t\tHCON:         0x%08x\n",npscx_readl(host,HCON));
	seq_printf(sf,"\t\tUHSREG:       0x%08x\n",npscx_readl(host,UHS_REG));
	seq_printf(sf,"\t\tBMOD:         0x%08x\n",npscx_readl(host,BMOD));
	seq_printf(sf,"\t\tDBADDR:       0x%08x\n",npscx_readl(host,DBADDR));
	seq_printf(sf,"\t\tIDSTS:        0x%08x\n",npscx_readl(host,IDSTS));
	seq_printf(sf,"\t\tIDINTEN:      0x%08x\n",npscx_readl(host,IDINTEN));
	seq_printf(sf,"\t\tDSCADDR:      0x%08x\n",npscx_readl(host,DSCADDR));
	seq_printf(sf,"\t\tBUFADDR:      0x%08x\n",npscx_readl(host,BUFADDR));
	seq_printf(sf,"\t\tCARDTHCTL:    0x%08x\n",npscx_readl(host,CARDTHRCTL));
	seq_printf(sf,"\t\tUHSREGEXT:    0x%08x\n",npscx_readl(host,UHS_REG_EXT));
	seq_printf(sf,"\t\tEMMCDDRREG:   0x%08x\n",npscx_readl(host,EMMC_DDR_REG));
	seq_printf(sf,"\t\tENABLESHIFT:  0x%08x\n",npscx_readl(host,ENABLE_SHIFT));


	value = npscx_readl(host,STATUS);
	value = (value >> 4) & 0xf;
	seq_printf(sf,"\t\t----CMD FSM STATUS  ---------\n ");
	if(value < 16)
		seq_printf(sf,"\t\t %s \n",npscxmci_cmd_fsm_status[value]);

	value = npscx_readl(host,IDSTS);
	value = (value >> 13) & 0xf;
	seq_printf(sf,"\t\t----DMA FSM STATUS ---------\n");
	if(value < 9)
		seq_printf(sf,"\t\t %s \n",npscxmci_dmac_fsm_status[value]);

	/*
	 *TODO : pmruntime put
	 */

	return 0;

}

static int npscxmci_registers_open(struct inode *inode,struct file *file)
{
	return single_open(file,npscxmci_registers_show,inode->i_private);
}

static struct file_operations npscxmci_registers_ops = {
	.open = npscxmci_registers_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
static int npscxmci_state_show(struct seq_file *sf,void *data)
{

	struct npscxmci_host *host = sf->private;
	seq_printf(sf,"\t\tflags :	0x%08x\n",host->flags);
	seq_printf(sf,"\t\tstate :	0x%08x\n",host->state);
	seq_printf(sf,"\t\taccomplished:0x%08x\n",host->accomplish_events);
	seq_printf(sf,"\t\tarchives :	0x%08x\n",host->archives_events);


	return 0;
}

static int npscxmci_state_open(struct inode *inode,struct file *file)
{
	return single_open(file,npscxmci_state_show,inode->i_private);
}

static const struct file_operations npscxmci_state_ops = {
	.open = npscxmci_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};


static int npscxmci_init_debugfs(struct npscxmci_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct dentry	*root;
	if(!mmc->debugfs_root){
		npscxmci_debug(host,dwarn,"%s has't resiger debug filesystem\n",mmc_hostname(mmc));
		return -ENODEV;
	}
	root = mmc->debugfs_root;
	if(!debugfs_create_file("npscxmci_registers",S_IRUSR,root,host,&npscxmci_registers_ops)){
		npscxmci_debug(host,derror,"create register debugfs failed\n");
		goto error_node;
	}
	if(!debugfs_create_file("npscxmci_state",S_IRUSR,root,host,&npscxmci_state_ops)){
		npscxmci_debug(host,derror,"create state debugfs failed");
		goto error_node;
	}

	return 0;

error_node:
	return -EFAULT;
}
#endif

static struct mmc_host_ops npscxmci_ops = {
	.request = npscxmci_request,
	.get_cd = npscxmci_get_cd,
	.get_ro = npscxmci_get_ro,
	.set_ios = npscxmci_set_ios,
	.start_signal_voltage_switch = npscxmci_start_signal_voltage_switch,
	.card_busy = npscxmci_card_busy,
	.enable_sdio_irq = npscxmci_enable_sdio_irq,
	.execute_tuning = npscxmci_execute_tuning,
};

static u32 npscxmci_obtain_hw_configuration(struct npscxmci_host *host)
{
	u32 value;
	u32 bits;
	const char *dma_intf[4] = {
		"NONE",
		"DW_DMA",
		"GENERIC_DMA",
		"NON_DW_DMA",
	};
	const char *data_width[8] = {
		"16bits",
		"32bits",
		"64bits",
		[3 ... 7] = "reserved",
	};

	value = npscx_readl(host,HCON);
	bits = value & 0x1;
	npscxmci_debug(host,ddebug,"\t\t card type : %s \n",bits ? "SD_MMC":"MMC_ONLY");
	bits = (value >> 1) & 0x1f;
	npscxmci_debug(host,ddebug,"\t\t card numbers : %d \n",bits + 1);
	bits = (value >> 6) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t h-bus-type : %s \n",bits ? "AHB" : "APB");
	bits = (value >> 7) & 0x7;
	npscxmci_debug(host,ddebug,"\t\t h-data-width : %s\n",data_width[bits]);
	bits = (value >> 10) & 0x3f;
	npscxmci_debug(host,ddebug,"\t\t h-addr-width : %dbits\n",bits + 1);
	bits = (value >> 16) & 0x3;
	npscxmci_debug(host,ddebug,"\t\t dma-interface : %s\n",dma_intf[bits]);
	bits = (value >> 18) & 0x7;
	npscxmci_debug(host,ddebug,"\t\t gen-dma-data-width : %s\n",data_width[bits]);
	bits = (value >> 21) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t fifo ram location: %s\n",bits ? "inside" : "outside");
	bits = (value >> 22) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t implement hold reg : %s\n",bits ? "true" : "false");
	bits = (value >> 23) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t set clk false path : %s \n",bits ? "trus" : "false");
	bits = (value >> 24) & 0x3;
	npscxmci_debug(host,ddebug,"\t\t clk divider numbers : %d \n",bits + 1);
	bits = (value >> 26) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t area optimized : %s\n",bits ? "true" : "false");
	bits = (value >> 27) & 0x1;
	npscxmci_debug(host,ddebug,"\t\t addr config : %d bit address supported\n",bits ? 64 : 32);
	return value;


}

static ssize_t set_dll_config_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct npscxmci_host *host;
	int drv_clk;

	host = dev_get_drvdata(dev);

	drv_clk = simple_strtoul(buf, NULL, 10);
	set_dll_config(host, SDMMC_DLL_MASTER_CTRL_VAL, 0x30, 0x00, drv_clk);
	pr_err("%s:the drv_clk is %d\n", __func__, drv_clk);

	return count;
};


static DEVICE_ATTR(set_dll_config, S_IWUSR, NULL, set_dll_config_store);

static set_pin_drv_stren(struct npscxmci_host *host)
{
	unsigned int val;

	val = readl(host->scm_drv_stren_vaddr);
	val |= 0x80000000;
	writel(val, host->scm_drv_stren_vaddr);

	val = 0x24924924;
	writel(val, host->scm_drv_stren_vaddr + 4);

}

static int npscxmci_probe(struct platform_device *pdev)
{
	struct npscxmci_host *host;
	struct npscxmci_pltfm_data *pri_data;
	struct resource  *res_mem;
	struct mmc_host *mmc;
	int result = 0;
	pri_data = pdev->dev.platform_data;
	if(!pri_data){
		dev_info(&pdev->dev,"(%s) no platform data ",dev_name(&pdev->dev));
		pri_data = kzalloc(sizeof(*pri_data),GFP_KERNEL);
		if(!pri_data){
			dev_err(&pdev->dev,"request memory for platform data failed\n");
			return -ENOMEM;
		}
	}

	pdev->id = 1;
	pdev->dev.id = 1;
	mmc = mmc_alloc_host(sizeof(*host),&pdev->dev);
	if(!mmc){
		dev_err(&pdev->dev,"alloc host failed\n");
		result = -ENOMEM;
		goto error_alloc_mmc;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = pdev;
	host->prv_data = pri_data;

	res_mem = platform_get_resource(pdev,IORESOURCE_MEM,0);
	if(!res_mem){
		dev_err(&pdev->dev,"can't get platform resource\n");
		result =  -EINVAL;
		goto error_no_res;
	}

	host->irq = platform_get_irq(pdev,0);
	if(host->irq < 0){
		dev_err(&pdev->dev,"can't get platform irq \n");
		result = -ENXIO;
		goto error_no_res;
	}else
		dev_dbg(&pdev->dev,"irq = %d\n",host->irq);

	host->clk = devm_clk_get(&pdev->dev, "emmc_clk");
	if (IS_ERR(host->clk))
		dev_err(&pdev->dev, "no emmc_clk\n");

	clk_set_rate(host->clk, EMMC_MAX_CLK);
	clk_prepare_enable(host->clk);

	result = npscxmci_of_parse(host,pri_data);
	if(result){
		dev_err(&pdev->dev,"parse dts failed\n");
		goto error_no_res;
	}

	host->sd_clk_ctrl_vaddr = devm_ioremap(&pdev->dev, SD_CLK_CTRL_ADDR, sizeof(unsigned int));
	if (IS_ERR(host->sd_clk_ctrl_vaddr)) {
		dev_err(&pdev->dev, "sd_clk_ctrl_vaddr ioremap failed\n");
		goto error_no_res;
	}

	host->scm_drv_stren_vaddr = devm_ioremap(&pdev->dev, SCM_DRV_STREN_ADDR, 2 * sizeof(unsigned int));
	if (IS_ERR(host->scm_drv_stren_vaddr)) {
		dev_err(&pdev->dev, "scm_drv_stren_vaddr ioremap failed\n");
		goto error_no_res;
	}

	/**
	 * init dma intface type
	 */
	host->dma_intf = GEN_DMA_INTF;

	host->caps.auto_stop_cmd = true;

	/**
	 *maximum number of segments,soft limits to  512.
	 */

	mmc->max_segs = DMA_MAX_IDMAC_DESC_LEN / sizeof(struct npscxmci_dma_desc);

	/**
	 *maximum segment size,each descriptor can transfer up to 4kB of data in the
	 * chain mode.
	 */
	mmc->max_seg_size = 4096;
	/**
	 *maximum block count
	 */
	mmc->max_blk_count = mmc->max_segs;

	/**
	 *maximum block size,the hardware limits to 65535 bytes
	 */
	mmc->max_blk_size = 65535;

	/**
	 *maximum request size every transfer.
	 */
	mmc->max_req_size = mmc->max_segs * mmc->max_seg_size;

	/**
	 * dma descriptor set up.
	 */
	result = npscxmci_dma_desc_setup(host);
	if(result)
		goto  error_no_res;

	mmc->ops = &npscxmci_ops;
	if(!request_mem_region(res_mem->start,resource_size(res_mem),dev_name(&pdev->dev))){
		dev_err(&pdev->dev,"can't result memory region\n");
		result = -EBUSY;
		goto error_res_busy;
	}

	host->iobase = ioremap(res_mem->start,resource_size(res_mem));
	if(!host->iobase){
		dev_err(&pdev->dev,"ioremap error");
		result = -ENOMEM;
		goto error_remap;
	}

/*	host->vcmmc = regulator_get(&pdev->dev,"vcmmc");
	if(IS_ERR_OR_NULL(host->vcmmc)){
		dev_warn(&pdev->dev,"can't find vcmmc regulator\n");
		host->vcmmc = NULL;
	}

	host->vqmmc = regulator_get(&pdev->dev,"vqmmc");
	if(IS_ERR_OR_NULL(host->vqmmc)){
		dev_warn(&pdev->dev,"can't find vqmmc regulator\n");
		host->vqmmc = NULL;
	}else{
		regulator_enable(host->vqmmc);
	}*/
	spin_lock_init(&host->lock);


	INIT_LIST_HEAD(&host->node);
	tasklet_init(&host->state_tasklet,npscxmci_state_machine_tasklet,(unsigned long)host);
	tasklet_init(&host->cd_tasklet,npscxmci_detect_card_tasklet,(unsigned long)host);
	setup_timer(&host->monitor_timer,npscxmci_monitor_timer,(unsigned long)host);


	if(npscx_card_removal(pri_data->type)){
		if(__npscxmci_get_cd(host)){
			dev_dbg(&pdev->dev,"%s is present when power on\n",mmc_hostname(mmc));
			NPSCXMCI_SET_FLAGS(host,FLAGS_CARD_PRESENT);
		}
	}else if(npscx_card_softplug(pri_data->type)){
		list_add_tail(&host->node,&npscxmci_softplug_list);
		NPSCXMCI_SET_FLAGS(host,FLAGS_CARD_PRESENT);
	}
	/*
	 * init controller
	 */
	dev_dbg(&pdev->dev,"host->irq = %d\n",host->irq);
	result = request_irq(host->irq,npscxmci_irq,IRQF_SHARED,dev_name(&pdev->dev),(void *)host);
	if(result){
		dev_err(&pdev->dev,"request interrupt failed\n");
		goto error_req_irq;
	}

	if(gpio_is_valid(pri_data->cd_pin.gpio)){
		result = request_irq(pri_data->irq_cd,npscxmci_card_detect_irq,IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,dev_name(&pdev->dev),(void *)host);
		if(result)
			goto error_req_gpioirq;
	}
	platform_set_drvdata(pdev,host);

	npscxmci_obtain_hw_configuration(host);

	npscxmci_init(host);
	set_pin_drv_stren(host);
	mmc_add_host(mmc);

#ifdef	CONFIG_DEBUG_FS
	npscxmci_init_debugfs(host);
#endif
	device_create_file(&pdev->dev, &dev_attr_set_dll_config);
	return 0;
error_req_gpioirq:
	free_irq(host->irq,(void *)host);
error_req_irq:
	//del_timer_sync();
	tasklet_kill(&host->state_tasklet);
	tasklet_kill(&host->cd_tasklet);
	if(host->vqmmc){
		regulator_disable(host->vqmmc);
		regulator_put(host->vqmmc);
	}
	if(host->vcmmc){
		regulator_put(host->vcmmc);
	}
	iounmap(host->iobase);
error_remap:
	release_mem_region(res_mem->start,resource_size(res_mem));
error_res_busy:
	if(unlikely(host->dma_allign_pad)){
		host->dma_desc_paddr -= host->dma_allign_pad;
		host->dma_desc_vaddr = (u32)host->dma_desc_vaddr - host->dma_allign_pad;
	}
	dma_free_coherent(mmc_dev(mmc),host->dma_desc_length,host->dma_desc_vaddr,host->dma_desc_paddr);
error_no_res:
	mmc_free_host(mmc);
error_alloc_mmc:
	kfree(pri_data);

	if (!IS_ERR(host->clk))
		clk_disable_unprepare(host->clk);
	return result;
}

static void npscxmci_remove(struct platform_device *pdev)
{
	struct npscxmci_host *np_host;
	struct npscxmci_pltfm_data *private;
	struct resource *res;
	res = platform_get_resource(pdev,IORESOURCE_MEM,0);
	np_host = platform_get_drvdata(pdev);
	private = np_host->prv_data;
	BUG_ON(!np_host);
	mmc_remove_host(np_host->mmc);
	npscxmci_disable_contrl(np_host);
	free_irq(np_host->irq,np_host);
	if(gpio_is_valid(private->cd_pin.gpio))
		free_irq(private->irq_cd,np_host);
	tasklet_kill(&np_host->state_tasklet);
	tasklet_kill(&np_host->cd_tasklet);
	del_timer_sync(&np_host->monitor_timer);
	list_del(&np_host->node);
	if(np_host->vcmmc){
		if(regulator_is_enabled(np_host->vcmmc))
			regulator_disable(np_host->vcmmc);
		regulator_put(np_host->vcmmc);
	}
	if(np_host->vqmmc){
		regulator_disable(np_host->vqmmc);
		regulator_put(np_host->vqmmc);
	}
	iounmap(np_host->iobase);
	release_mem_region(res->start,resource_size(res));
	kfree(private);
	if(unlikely(np_host->dma_allign_pad)){
		np_host->dma_desc_paddr -= np_host->dma_allign_pad;
		np_host->dma_desc_vaddr = (u32)np_host->dma_desc_vaddr - np_host->dma_allign_pad;
	}
	dma_free_coherent(mmc_dev(np_host->mmc),np_host->dma_desc_length,np_host->dma_desc_vaddr,np_host->dma_desc_paddr);
	mmc_free_host(np_host->mmc);

	if(!IS_ERR(np_host->clk))
		clk_disable_unprepare(np_host->clk);

	platform_set_drvdata(pdev,NULL);



}

#ifdef CONFIG_PM
static int npscxmci_suspend(struct device *dev)
{
	int retval;
	struct npscxmci_host *host = dev_get_drvdata(dev);
	struct mmc_host *mmc = host->mmc;
	npscxmci_debug(host,dinfo,"[%s] Sunspend process \n",mmc_hostname(host->mmc));

	retval = mmc_suspend_host(mmc);
	if(retval){
		npscxmci_debug(host,derror,"[%s ]Suspend failed\n",mmc_hostname(mmc));
		return retval;
	}

	if(!(mmc->pm_flags & MMC_PM_KEEP_POWER)){
		//if(host->vcmmc){
		//	if(regulator_is_enabled(host->vcmmc))
		//		regulator_disable(host->vcmmc);
		//}
	}

	npscx_writel(host,RINTSTS,0xffffffff);
	/*Mask all interrupts*/
	npscx_writel(host,INTMASK,0x0);
	/*
	 * clear all pendings interrupts for DMA.
	 */
	npscx_writel(host,IDSTS,0x3ff);

	/**
	 *disable dma interrupts
	 */
	npscx_writel(host,IDINTEN,0x0);
	/*
	 *disable global interrupt
	 */
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_INT_ENABLE,0);

	//free_irq(host->irq,host);
	if(gpio_is_valid(host->prv_data->cd_pin.gpio)){
		//free_irq(private->cd_pin.irq_cd,np_host)
		disable_irq(host->prv_data->irq_cd);
	}
	return 0;


}


static int npscxmci_resume(struct device *dev)
{
	struct npscxmci_host *host = dev_get_drvdata(dev);
	struct mmc_host *mmc = host->mmc;
	int retval;
	npscxmci_debug(host,dinfo,"[%s] Resume process\n",mmc_hostname(mmc));

	if(gpio_is_valid(host->prv_data->cd_pin.gpio))
		enable_irq(host->prv_data->irq_cd);

	if (!IS_ERR(host->clk)) {
		clk_set_rate(host->clk, EMMC_MAX_CLK);
		clk_prepare_enable(host->clk);
	}

	npscxmci_init(host);
	set_pin_drv_stren(host);
	if(mmc->pm_flags & MMC_PM_KEEP_POWER){
		__npscxmci_set_ios(mmc,&mmc->ios);
		__npscxmci_signal_voltage_switch(mmc,&mmc->ios);
	}

	retval = mmc_resume_host(mmc);
	if(retval){
		npscxmci_debug(host,derror,"[%s] Resume process failed\n",mmc_hostname(mmc));
		return retval;
	}


	return 0;

}


#endif
#ifdef		CONFIG_PM_RUNTIME
static int npscxmci_runtime_suspend(struct device *dev)
{
	struct npscxmci_host *host = dev_get_drvdata(dev);

	npscxmci_debug(host,dinfo,"[%s] Runtime suspend process\n",mmc_hostname(host->mmc));

	/*
	 *disable global interrupt
	 */
	npscxmci_clear_set(host,NPSCXMCI_CTRL,CTRL_INT_ENABLE,0);

	/**
	 *sync pending interrupt
	 */
	synchronize_irq(host->irq);
	/**
	 *after disable global interrupt and sync the interrupt,the irq would't happen.
	 */
	npscx_writel(host,RINTSTS,0xffffffff);
	/*Mask all interrupts*/
	npscx_writel(host,INTMASK,0x0);
	/*
	 * clear all pendings interrupts for DMA.
	 */
	npscx_writel(host,IDSTS,0x3ff);

	/**
	 *disable dma interrupts
	 */
	npscx_writel(host,IDINTEN,0x0);


	/**
	 *TODO : clock disable
	 */
	if (!IS_ERR(host->clk))
		clk_disable_unprepare(host->clk);

	return 0;
}

static int npscxmci_runtime_resume(struct device *dev)
{
	struct npscxmci_host *host = dev_get_drvdata(dev);
	struct mmc_host *mmc = host->mmc;
	npscxmci_debug(host,dinfo,"[%s] Runtime resume process\n",mmc_hostname(host->mmc));

	npscxmci_init(host);

	__npscxmci_set_ios(mmc,&mmc->ios);
	__npscxmci_signal_voltage_switch(mmc,&mmc->ios);

	if(NPSCXMCI_TEST_FLAGS(host,FLAGS_SDIO_IRQ_ENABLED))
		npscxmci_enable_sdio_irq(mmc,true);

	/**
	 *TODO : clock enable
	 */
	if (!IS_ERR(host->clk))
		clk_prepare_enable(host->clk);
	return 0;
}
#endif

#ifdef		CONFIG_PM
static const struct dev_pm_ops npscxmci_pm_ops = {
	.suspend = npscxmci_suspend,
	.resume  = npscxmci_resume,
	SET_RUNTIME_PM_OPS(npscxmci_runtime_suspend,npscxmci_runtime_resume,NULL)
};

#define		NPSCXMCI_PM_OPS			(&npscxmci_pm_ops)
#else
#define		NPSCXMCI_PM_OPS			NULL
#endif

static const struct of_device_id npscx_of_match[]  = {
	{.compatible = "nufront,npscxmci",},
	{},
};

MODULE_DEVICE_TABLE(of, npscx_of_match);
static struct platform_driver npscxmci_driver = {
	.probe = npscxmci_probe,
	.remove = __exit_p(npscxmci_remove),
	.driver = {
		.name = "NPSCX-MCI",
		//.owner = THIS_OWNER,
		.owner = THIS_MODULE,
		.of_match_table = npscx_of_match,
		.pm = NPSCXMCI_PM_OPS,
	}
};

static int  __init npscxmci_module_init(void)
{
	INIT_LIST_HEAD(&npscxmci_softplug_list);
	return platform_driver_register(&npscxmci_driver);
}

static void __exit npscxmci_module_exit(void)
{
	platform_driver_unregister(&npscxmci_driver);
}

module_init(npscxmci_module_init);
module_exit(npscxmci_module_exit);
MODULE_AUTHOR("yangjun <jun.yang@nufront.com>");
MODULE_DESCRIPTION("Meltimedia Controller Interface core driver");
MODULE_LICENSE("GPL v2");
