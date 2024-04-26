/*
 * MMC request state machine
 *
 */

/*
 * Register address offset
 */
#ifndef		__NPSCXMCI_H
#define		__NPSCXMCI_H
#define		NPSCXMCI_CTRL		0x00
#define		NPSCXMCI_PWREN		0x04
#define		NPSCXMCI_CLKDIV		0x08
#define		NPSCXMCI_CLKSRC		0x0c
#define		NPSCXMCI_CLKENA		0x10
#define		NPSCXMCI_TMOUT		0x14
#define		NPSCXMCI_CTYPE		0x18
#define		NPSCXMCI_BLKSIZ		0x1c
#define		NPSCXMCI_BYTCNT		0x20
#define		NPSCXMCI_INTMASK	0x24
#define		NPSCXMCI_CMDARG		0x28
#define		NPSCXMCI_CMD		0x2c
#define		NPSCXMCI_RESP0		0x30
#define		NPSCXMCI_RESP1		0x34
#define		NPSCXMCI_RESP2		0x38
#define		NPSCXMCI_RESP3		0x3c
#define		NPSCXMCI_MINTSTS	0x40
#define		NPSCXMCI_RINTSTS	0x44
#define		NPSCXMCI_STATUS		0x48
#define		NPSCXMCI_FIFOTH		0x4c
#define		NPSCXMCI_CDETECT	0x50
#define		NPSCXMCI_WRTPRT		0x54
#define		NPSCXMCI_GPIO		0x58
#define		NPSCXMCI_TCBCNT		0x5c /* ro*/
#define		NPSCXMCI_TBBCNT		0x60 /* ro */
#define		NPSCXMCI_DEBNCE		0x64	/*rw*/
#define		NPSCXMCI_USRID		0x68	/*rw*/
#define		NPSCXMCI_VERID		0x6c	/*ro*/
#define		NPSCXMCI_HCON		0x70	/*ro*/
#define		NPSCXMCI_UHS_REG	0x74    /*rw*/
#define		NPSCXMCI_RSTn		0x78	/*rw*/
#define		NPSCXMCI_BMOD		0x80	/*rw*/
#define		NPSCXMCI_PLDMND		0x84	/*rw*/
#define		NPSCXMCI_DBADDR		0x88	/*rw*/
#define		NPSCXMCI_IDSTS		0x8c	/*rw*/
#define		NPSCXMCI_IDINTEN	0x90	/*rw*/
#define		NPSCXMCI_DSCADDR	0x94	/*rw*/
#define		NPSCXMCI_BUFADDR	0x98	/*rw*/
#define		NPSCXMCI_CARDTHRCTL	0x100	/*rw*/
#define		NPSCXMCI_BEPOWER	0x104	/*rw back-end-power */
#define		NPSCXMCI_UHS_REG_EXT	0x108	/*rw*/
#define		NPSCXMCI_EMMC_DDR_REG	0x10c	/*rw*/
#define		NPSCXMCI_ENABLE_SHIFT	0x110	/*rw*/
#define		NPSCXMCI_SDMMC_DLL_MASTER_CTRL   0x114
#define		NPSCXMCI_SDMMC_STB_PHS_CTRL      0x118
#define		NPSCXMCI_SDMMC_RSYNC_DLL         0x11c
#define		NPSCXMCI_SDMMC_RESET_DLL         0x120
#define		NPSCXMCI_SDMMC_DLL_LOCK		 0x130

#define		NPSCXMCI_DATA_FIFO	0x200


/*
 * Register bits
 *
 */

/*
 * ctrl register
 */
#define		CTRL_USE_IDMAC		(1 << 25)
#define		CTRL_EOD_PUP		(1  << 24 )
#define		CTRL_VOLTAGE_B(x)	(((x) & 0xf) << 20)
#define		CTRL_VOLTAGE_A(x)	((x)<< 16)
#define		CTRL_ABORT_RDATA	(1 << 8)
#define		CTRL_SEND_IRQRSP	(1 << 7)
#define		CTRL_READ_WAIT		(1 << 6)
#define		CTRL_DMA_ENABLE		(1 << 5)
#define		CTRL_INT_ENABLE		(1 << 4)
#define		CTRL_DMA_RST		(1 << 2)
#define		CTRL_FIFO_RST		(1 << 1)
#define		CTRL_HOST_RST		(1 << 0)

/*
 *PWREN register
 */
#define		PWREN_ENABLE		(1 << 0) //only one card

/*
 *CLKDIV REGISTER
 */
#define		CLKDIV_DIVIDER0(x)	((x) << 0) //only one card

/*
 *CLKSRC REGISTER
 */
#define		CLKSRC_SRC0		((x) << 0) //only one card

/*
 *CLKENA REGISTER
 */
#define		CLKENA_LPOWER0		(1 << 16) // only one card
#define		CLKENA_ENABLE0		(1 << 0)

/*
 *TMOUT REGISTER
 */
#define		TMOUT_DATA(x)		((x) << 8)
#define		TMOUT_RSP(x)		((x) << 0)
#define		TMOUT_DATA_MASK(x)	((x) & 0xffffff)
#define		TMOUT_RSP_MASK(x)	((x) & 0xff)

/*
 *CTYPE	REGISTER
 */

#define		CTYPE_8BIT		(1 << 16)
#define		CTYPE_4BIT		(1 << 0) //ONLY one card

/*
 *INTMASK REGISTER
 */
#define		INTMASK_SDIO0		(1 << 16)
#define		INTMASK_EBE		(1 << 15)
#define		INTMASK_ACD		(1 << 14)
#define		INTMASK_BCI		(1 << 13)
#define		INTMASK_HLE		(1 << 12)
#define		INTMASK_FRUN		(1 << 11)
#define		INTMASK_HTO		(1 << 10)
#define		INTMASK_DRTO		(1 << 9)
#define		INTMASK_RTO		(1 << 8)
#define		INTMASK_DCRC		(1 << 7)
#define		INTMASK_RCRC		(1 << 6)
#define		INTMASK_RXDR		(1 << 5)
#define		INTMASK_TXDR		(1 << 4)
#define		INTMASK_DTO		(1 << 3)
#define		INTMASK_CC		(1 << 2)
#define		INTMASK_RE		(1 << 1)
#define		INTMASK_CD		(1 << 0)

/*
 *CMD REGISTER
 */

#define		CMD_START		(1 << 31)
#define		CMD_USE_HOLD_REG	(1 << 29)
#define		CMD_VOLT_SWITCH		(1 << 28)
#define		CMD_BOOTM		(1 << 27)
#define		CMD_BOOT_DIS		(1 << 26)
#define		CMD_EBOOT_ACK		(1 << 25)
#define		CMD_BOOT_EN		(1 << 24)
#define		CMD_UPCLK_ONLY		(1 << 21)
#define		CMD_SEND_INIT		(1 << 15)
#define		CMD_STOP_ABORT		(1 << 14)
#define		CMD_WAIT_PRVDATA	(1 << 13)
#define		CMD_AUTO_STOP		(1 << 12)
#define		CMD_TRAN_MODE(x)	((x) << 11)
#define		CMD_RW(x)		((x) << 10)
#define		CMD_DATA_EXP		(1 << 9)
#define		CMD_CHECK_CRC		(1 << 8)
#define		CMD_RSP_LONG		(1 << 7)
#define		CMD_RSP_EXP		(1 << 6)
#define		CMD_INDEX(x)		((x) << 0)



/*
 *MINTSTS REGISTER
 */
#define		MINTSTS_SDIO0		(1 << 16)
#define		MINTSTS_EBE		(1 << 15)
#define		MINTSTS_ACD		(1 << 14)
#define		MINTSTS_BCI		(1 << 13)
#define		MINTSTS_HLE		(1 << 12)
#define		MINTSTS_FRUN		(1 << 11)
#define		MINTSTS_HTO		(1 << 10)
#define		MINTSTS_DRTO		(1 << 9)
#define		MINTSTS_RTO		(1 << 8)
#define		MINTSTS_DCRC		(1 << 7)
#define		MINTSTS_RCRC		(1 << 6)
#define		MINTSTS_RXDR		(1 << 5)
#define		MINTSTS_TXDR		(1 << 4)
#define		MINTSTS_DTO		(1 << 3)
#define		MINTSTS_CC		(1 << 2)
#define		MINTSTS_RE		(1 << 1)
#define		MINTSTS_CD		(1 << 0)

/*
 *RINTSTS REGISTER
 */
#define		RINTSTS_SDIO0		(1 << 16)

#define		RINTSTS_EBE		(1 << 15)
#define		RINTSTS_ACD		(1 << 14)
#define		RINTSTS_BCI		(1 << 13)
#define		RINTSTS_HLE		(1 << 12)
#define		RINTSTS_FRUN		(1 << 11)
#define		RINTSTS_HTO		(1 << 10)
#define		RINTSTS_DRTO		(1 << 9)
#define		RINTSTS_RTO		(1 << 8)
#define		RINTSTS_DCRC		(1 << 7)
#define		RINTSTS_RCRC		(1 << 6)
#define		RINTSTS_RXDR		(1 << 5)
#define		RINTSTS_TXDR		(1 << 4)
#define		RINTSTS_DTO		(1 << 3)
#define		RINTSTS_CC		(1 << 2)
#define		RINTSTS_RE		(1 << 1)
#define		RINTSTS_CD		(1 << 0)

/*
 *STATUS REGISTER
 */
#define		STATUS_DMA_REQ		(1 << 31)
#define		STATUS_DMA_ACK		(1 << 30)
//#define		STATUS_FIFO_CNT		()
#define		STATUS_FIFO_CNT_MASK	0x1fff
#define		STATUS_MC_BUSY		(1 << 10)
#define		STATUS_DATA_BUSY	(1 << 9)
#define		STATUS_DATA3_BUSY	(1 << 8)
#define		STATUS_CMD_FSMS_MASK	0xf
#define		STATUS_FIFO_FULL	(1 << 3)
#define		STATUS_FIFO_EMPTY	(1 << 2)
#define		STATUS_FIFO_TXMARK	(1 << 1)
#define		STATUS_FIFO_RXMARK	(1 << 0)

/*
 *FIFOTH REGISTER
 */
#define		FIFOTH_MSIZE(x)		((x) << 28)
#define		FIFOTH_RX_WMARK(x)	((x) << 16)
#define		FIFOTH_TX_WMARK(x)	((x) << 0)

/*
 *CDETECT REGISTER
 */
#define		CDETECT_CARD_REMOVE	(1 << 0)

/*
 *WRTPRT REGISTER
 */
#define		WRTPRT_PROTECT		(1 << 0)

/*
 *DEBNCE REGISTER
 */
#define		DEBNCE_CNT_MASK		(0xffffff)

/*
 *HCON REGISTER
 */

#define		HCON_FIFO_RAM_INSIDE	(1 << 21)
#define		HCON_SUPORT_HOLD_REG	(1 << 22)
#define		HCON_FALSE_PATH		(1 << 23)
#define		HCON_AREA_OPTIMIZED	(1 << 26)
#define		HCON_ADDR_CONF		(1 << 27)
/*
 *DMA INTERFACE MODE
 */
#define		DW_DMA_INTF		0x01
#define		GEN_DMA_INTF		0x02
#define		NON_DW_INTF		0x03

/*
 *UHS_REG REGISTER
 */
#define		UHS_DDR_REG		(1 << 16)
#define		UHS_REG_1V8		(1 << 0)

/*
 *RSTn register
 */
#define		RSTn_CARD		(1 << 0)

/*
 *BMOD REGISTER
 */
#define		BMOD_PBL(x)		((x) << 8)
#define		BMOD_IDMA_EN		(1 << 7)
#define		BMOD_FB			(1 << 1)
#define		BMOD_DMA_RST		(1 << 0)

/*
 *PLDMND REGISTER
 */
#define		PLDMND_VALUE		(1 << 0)

/*
 *IDSTS REGISTER
 */

#define		IDSTS_AIS		(1 << 9)
#define		IDSTS_NIS		(1 << 8)
#define		IDSTS_CES		(1 << 5)
#define		IDSTS_DU		(1 << 4)
#define		IDSTS_FBE		(1 << 2)
#define		IDSTS_RI		(1 << 1)
#define		IDSTS_TI		(1 << 0)


/*
 *IDINTEN REGISTER
 */

#define		IDINTEN_AI		(1 << 9)
#define		IDINTEN_NI		(1 << 8)
#define		IDINTEN_CES		(1 << 5)
#define		IDINTEN_DU		(1 << 4)
#define		IDINTEN_FBE		(1 << 2)
#define		IDINTEN_RI		(1 << 1)
#define		IDINTEN_TI		(1 << 0)

/*
 *CARDTHRCTRL REGISTER
 */

#define		CARDTHRCTL_HOLD(x)	((x) << 16)
#define		CARDTHRCTL_WEN		(1 << 2)
#define		CARDTHRCTL_BCIEN	(1 << 1)
#define		CARDTHRCTL_REN		(1 << 0)


/*
 *BACK END POWER
 */


#define		BACK_EPOWER_EN		(1 << 0)

/*
 *UHS REG EXT REGISTER
 */
#define		EUHS_CLK_MUX(x)		((x) << 30)
#define		EUHS_CLK_DRV_PHASE(x)	((x) << 23)
#define		EUHS_CLK_SMPL_PHASE(x)	((x) << 16)
#define		EUHS_MMC_VOLT_1V2	(1 << 0)

/*
 *EMMC_DDR_REG REGISTER
 */
#define		EMMC_DDR_HS400		(1 << 31)
#define		EMMC_DDR_HSTART		(1 << 0)

/*
 *ENABLE SHIFT REGISTER
 */
//#define		ENSHFIT

/*
 * WRITE REGISTER
 */

#define		npscx_writel(host,reg,value) \
		writel((value) , (host)->iobase + NPSCXMCI_##reg)

#define		npscx_readl(host,reg) \
		readl((host)->iobase + NPSCXMCI_##reg)

/*sd_clk_ctrl register , scm drv strength register and dll register*/
#define SD_CLK_CTRL_ADDR 0x5200184
#define SCM_DRV_STREN_ADDR 0x5210084
#define SDMMC_DLL_MASTER_CTRL_VAL 0x0078041F
#define SDMMC_STB_PHS_CTRL_VAL    0x0000001F

#define EMMC_DLL_OUT_EN	(1 << 13)
#define EMMC_DLL_RESET_RELEASE 1
#define SDMMC_DLL_LOCKED       1
#define SDMMC_RSYNC_DLLL_EN    1
#define CLK_DRV_PHASE_MASK     0x7f
#define CLK_DRV_PHASE_SHIF     23
#define CLK_SMPL_PHASE_MASK    0x7f
#define CLK_SMPL_PHASE_SHIF    16



#define		npscx_card_mmc(type)	((type) & NPSCX_TYPE_MMC)
#define		npscx_card_sd(type)	((type) & NPSCX_TYPE_SD)
#define		npscx_card_sdio(type)	((type) & NPSCX_TYPE_SDIO)
#define		npscx_card_removal(type)	((type) & NPSCX_TYPE_REMOVAL)
#define		npscx_card_softplug(type)	((type) & NPSCX_TYPE_SOFT_PLUG)
#define		npscx_card_with_wp(type)	((type) & NPSCX_TYPE_WITH_WP)

enum	npscxmci_state{
	STATE_IDLE,
	STATE_WAITING_RSP,
	STATE_XFER_DATA,
	STATE_WAITING_DATA_DONE,
	STATE_WAITING_STOP_RSP,
	STATE_WAIT_BUSYDONE,
	STATE_DATA_ERROR,
	STATE_REQUEST_DONE,
	/*
	 *The two states are only for Voltage Switch (3.3v -> 1.8v)
	 */
	//STATE_WAITING_VOLT_SWITCH_LOW,
	//STATE_WAIT_VOLT_SWITCH_FINISH,
};

enum	npscxmci_events{
	EVENT_COMMAND_DONE,
	EVENT_BUSY_DONE,
	EVENT_XFER_DONE,
	EVENT_DATA_ERROR,
	EVENT_DMA_FINISHED,
	EVENT_DATA_DONE,
	/*
	 *The events only for Voltage Switch(3.3v -> 1.8v)
	 */
	EVENT_VOLT_SWITCH_INT,
	//EVENT_VOLT_SWITCH_INT_HIGH,
};


enum	npscxmci_card_state{
	STATE_CARD_NO_PRESENT = 1,
	STATE_CARD_PRESENT ,
};

enum	npscxmci_host_flags{
	FLAGS_SND_INIT,
	FLAGS_CARD_PRESENT,
	FLAGS_BROKEN_TIMEOUT,
	FLAGS_USE_DMA_TRAN,
	FLAGS_DOING_VOLT_SWITCH_LOW,
	FLAGS_DOING_VOLT_SWITCH_HIGH,
	FLAGS_SDIO_IRQ_ENABLED,
};

#define		CMD_ERROR_MASK		(MINTSTS_RTO | MINTSTS_RCRC | MINTSTS_RE | MINTSTS_HLE)

#define		CMD_DONE_MASK		(MINTSTS_CC)

#define		DATA_READ_ERROR_MASK		(MINTSTS_EBE | MINTSTS_BCI | MINTSTS_FRUN | MINTSTS_DRTO | MINTSTS_DCRC | MINTSTS_HTO )

#define		DATA_WRITE_ERROR_MASK		(MINTSTS_EBE | MINTSTS_FRUN |MINTSTS_DCRC | MINTSTS_HTO )


#define		DATA_DONE_MASK		(MINTSTS_DTO)

#define		DATA_READ_MASK		(MINTSTS_RXDR)

#define		DATA_WRITE_MASK		(MINTSTS_TXDR)

#define		BUSY_CLEAR_INT_MASK	(MINTSTS_BCI)

#define		SDIO_INT_MASK		(MINTSTS_SDIO0)

#define		CARD_DETECT_MASK		(MINTSTS_CD)

#define		VLOT_SWITCH_MASK	(MINTSTS_HTO)

#define		INT_INITIA_ENABLE	(INTMASK_EBE | INTMASK_FRUN | INTMASK_HTO | INTMASK_DRTO | INTMASK_HLE | INTMASK_RTO | \
						   INTMASK_DCRC | INTMASK_RCRC | INTMASK_DTO |INTMASK_CC |INTMASK_RE |INTMASK_CD | MINTSTS_BCI)
/*
 *Descriptor format for 32bit bus width
 */
struct npscxmci_dma_desc{
	u32	des0;
	u32	des1;
	u32	des2;
	u32	des3;
};

/**
 *DES0
 * */
#define		DES0_OWN	(1 << 31)
#define		DES0_ER		(1 << 5)
#define		DES0_CH		(1 << 4)
#define		DES0_FS		(1 << 3)
#define		DES0_LD		(1 << 2)
#define		DES0_DIC	(1 << 1)

/**
 *DES1
 * */
#define		DES1_BS1_SIZE(x)	((x) & 0x1fff)

struct npscxmci_host_caps{

	bool	auto_stop_cmd;

};

struct npscxmci_pin{
	int gpio;
	bool active_low;
};
struct npscxmci_pltfm_data{

	unsigned int	type;		/* card type*/
#define	NPSCX_TYPE_MMC		(1 << 0)		/*card mmc */
#define	NPSCX_TYPE_SD		(1 << 1)		/*card sd */
#define	NPSCX_TYPE_SDIO		(1 << 2)		/*card sdio */
#define	NPSCX_TYPE_REMOVAL	(1 << 3)		/*removable card */
#define	NPSCX_TYPE_SOFT_PLUG	(1 << 4)		/* soft hot plug*/
#define NPSCX_TYPE_WITH_WP	(1 << 5)                /* support write protect only in sd card*/
	struct npscxmci_pin	cd_pin;			/*card detect gpio*/
	struct npscxmci_pin	wp_pin;			/*write project gpio*/
	int    irq_cd;

};


struct npscxmci_host{

	/*
	 * desc addr must allign 4bytes,so the dma_desc_length must add (4 - 1) bytes
	 */
	unsigned int dma_desc_length;
	void	*dma_desc_vaddr;
	dma_addr_t	dma_desc_paddr;
	unsigned int	dma_allign_pad;
	void __iomem	*iobase;
	unsigned long	archives_events;
	unsigned long	accomplish_events;
	unsigned long	flags;
	//unsigned long	card_present;
	unsigned int	state;
	unsigned int	cmd_archives;
	unsigned int	data_archives;
	bool		soft_stop_cmd;
	//bool		need_wait_nobusy;
	enum	npscxmci_state	host_status;
	//enum	npscxmci_card_state card_present;
	unsigned int dma_intf;
	unsigned int fifo_depth;
	unsigned int dma_msize;
	unsigned int rx_wmark;
	unsigned int tx_wmark;
	unsigned int r_thold_size; // card threshold size. for  reading
	unsigned int w_thold_size; // card threshold size for writing.
	bool	 blksz_allign_dword; /*if use card read threshold,block size must allign DWORD*/
	bool	 r_thold_use; //when read in mode of SDR12 SDR25 DDR50 SDR50 SDR104 use
	bool	w_thold_use; // when write in mode HS400 Only use.

	struct clk *clk;

	struct scatterlist	*sg;
	struct sg_mapping_iter	sg_miter;
	int	sg_cnt;
	struct	regulator	*vqmmc;
	struct	regulator	*vcmmc;
	unsigned int		clk_src;
	unsigned int		clock;
	struct	npscxmci_host_caps caps;
	struct	npscxmci_pltfm_data *prv_data;

	struct	list_head	node;
	/*
	 * interrupt irq
	 */
	int irq;

	/**
	 * state machine tasklet
	 */
	struct tasklet_struct state_tasklet;

	/**
	 * card detect tasklet.
	 */
	struct tasklet_struct cd_tasklet;
	/**
	 * monitor timeout
	 */


	struct timer_list monitor_timer;

	struct platform_device	*pdev;

	void __iomem *sd_clk_ctrl_vaddr;
	void __iomem *scm_drv_stren_vaddr;

	spinlock_t	lock;
	/**
	 *mmc reference
	 */
	struct mmc_host *mmc;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_data *data;

	int (*prepare_data)(struct npscxmci_host *host ,struct mmc_command *cmd);
	int (*start_data)(struct npscxmci_host *host,struct mmc_command *cmd);
	int (*abort_data)(struct npscxmci_host *host,struct mmc_command *cmd);
	/*
	 * reset fifo or dma or controller
	 */
	int (*reset_fsm)(struct npscxmci_host *host);
	/*
	 * the request done ,call this for free controller.
	 */
	void (*cleanup)(struct npscxmci_host *host);
};

#endif
