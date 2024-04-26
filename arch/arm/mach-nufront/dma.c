/* linux/arch/arm/mach-nufront/dma.c
*/

#include <linux/dma-mapping.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl330.h>
#include <linux/amba/pl080.h>
#include <linux/amba/pl08x.h>
#include <asm/irq.h>

#include <mach/irqs.h>
#include <mach/dma.h>

#define IRQ_DMA0 (78+32)
#define PL330_BASE 0x02044000

//ARM primecell pl080 DMA defined Here
#define IRQ_OFFSET 32
#define IRQ_DMACITR_NUM  87
#define IRQ_DMACITR_PL080 (IRQ_DMACITR_NUM+IRQ_OFFSET)
#define PL080_BASE	(0x61B0000)


/*define current platfrom supported pl330 operation*/
static u8 npsc01_pl330_peri[] = {
	DMACH_MTOM_0,
	DMACH_MTOM_1,
	DMACH_MTOM_2,
	DMACH_MTOM_3,
	DMACH_MTOM_4,
	DMACH_MTOM_5,
	DMACH_MTOM_6,
	DMACH_MTOM_7
};

static struct dma_pl330_platdata npsc01_pl330_pdata = {
	.nr_valid_peri = ARRAY_SIZE(npsc01_pl330_peri),
	.peri_id = npsc01_pl330_peri,
};


static AMBA_AHB_DEVICE(npsc01_pl330, "dma-pl330", 0x00041330,
	PL330_BASE, {IRQ_DMA0}, NULL);


////////////////////////////////////////////////////////////
//PL080 DMA AHB device
struct pl08x_channel_data npsc01_dma_slave_channel[] = {
	{
		.bus_id = "uart0_rx",
		.min_signal = 0,
		.max_signal = 0,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart0_tx",
		.min_signal = 1,
		.max_signal = 1,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart1_rx",
		.min_signal = 2,
		.max_signal = 2,
	    .muxval = 0,
	    .periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart1_tx",
	    .min_signal = 3,
		.max_signal = 3,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart2_rx",
		.min_signal = 4,
	    .max_signal = 4,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart2_tx",
		.min_signal = 5,
		.max_signal = 5,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart3_rx",
		.min_signal = 6,
		.max_signal = 6,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "uart3_tx",
		.min_signal = 7,
		.max_signal = 7,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "spi0_rx",
		.min_signal = 10,
		.max_signal = 10,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "spi0_tx",
		.min_signal = 11,
		.max_signal = 11,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "spi1_rx",
		.min_signal = 12,
		.max_signal = 12,
		//.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "spi1_tx",
		.min_signal = 13,
		.max_signal = 13,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
    {
		.bus_id = "i2s_rx",
		.min_signal = 14,
		.max_signal = 14,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
	{
		.bus_id = "i2s_tx",
		.min_signal = 15,
		.max_signal = 15,
		.muxval = 0,
		.periph_buses = PL08X_AHB1,
	},
};

static int pl08x_get_signal(const struct pl08x_channel_data *cd)
{
	return cd->min_signal;
}

static void pl08x_put_signal(const struct pl08x_channel_data *cd, int ch)
{
}


struct pl08x_platform_data pl08x_plat_data = {
	.memcpy_channel = {
		.bus_id = "memcpy",
	/*	.cctl_memcpy =
			(PL080_BSIZE_16 << PL080_CONTROL_SB_SIZE_SHIFT | \
			PL080_BSIZE_16 << PL080_CONTROL_DB_SIZE_SHIFT | \
			PL080_WIDTH_32BIT << PL080_CONTROL_SWIDTH_SHIFT | \
			PL080_WIDTH_32BIT << PL080_CONTROL_DWIDTH_SHIFT | \
			PL080_CONTROL_PROT_BUFF | PL080_CONTROL_PROT_CACHE | \
			PL080_CONTROL_PROT_SYS), */
	},
	.slave_channels = &npsc01_dma_slave_channel[0],
	.num_slave_channels = ARRAY_SIZE(npsc01_dma_slave_channel),
	.get_signal = pl08x_get_signal,
	.put_signal = pl08x_put_signal,
	.lli_buses = PL08X_AHB1 | PL08X_AHB2, //PL08X_AHB2,
	.mem_buses = PL08X_AHB2, //PL08X_AHB2,
};

static AMBA_AHB_DEVICE(npsc01_pl08x, "pl08xdmac", 0x00041080,
			PL080_BASE, {IRQ_DMACITR_PL080}, &pl08x_plat_data);



static int __init npsc01_dma_init(void)
{
	//for dma pl080

	npsc01_pl08x_device.dev.platform_data = &pl08x_plat_data;
	amba_device_register(&npsc01_pl08x_device, &iomem_resource);

	// for dma pl330
	dma_cap_set(DMA_CYCLIC, npsc01_pl330_pdata.cap_mask);
	dma_cap_set(DMA_MEMCPY, npsc01_pl330_pdata.cap_mask);
	npsc01_pl330_device.dev.platform_data = &npsc01_pl330_pdata;

	amba_device_register(&npsc01_pl330_device, &iomem_resource);

	return 0;
}


arch_initcall(npsc01_dma_init);
