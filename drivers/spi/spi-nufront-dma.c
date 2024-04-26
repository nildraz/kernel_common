#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/types.h>
#include <linux/amba/pl08x.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "spi-dw.h"

#define SPI_DMACR_RDMAE		(1 << 0)
#define SPI_DMACR_TDMAE		(1 << 1)

static enum dma_slave_buswidth convert_dma_width(u32 dma_width)
{
	if (dma_width == 2)
		return DMA_SLAVE_BUSWIDTH_2_BYTES;

	return DMA_SLAVE_BUSWIDTH_1_BYTE;
}

static int npsc_spi_dma_setup(struct dw_spi *dws)
{
	struct dma_slave_config conf;

	/* Config tx channel */
	memset(&conf, 0, sizeof(conf));
	conf.direction = DMA_MEM_TO_DEV;
	conf.dst_addr = dws->dma_addr;
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.dst_addr_width = convert_dma_width(dws->dma_width);
	conf.dst_maxburst = 1;
	conf.device_fc = false;
	dmaengine_slave_config(dws->txchan, &conf);

	/* Config rx channel */
	memset(&conf, 0, sizeof(conf));
	conf.direction = DMA_DEV_TO_MEM;
	conf.src_addr = dws->dma_addr;
	conf.src_addr_width = convert_dma_width(dws->dma_width);
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_maxburst = 1;
	conf.device_fc = false;
	dmaengine_slave_config(dws->rxchan, &conf);

	dw_writew(dws, DW_SPI_DMATDLR, dws->fifo_len / 2);
	dw_writew(dws, DW_SPI_DMARDLR, 0);
	dw_writew(dws, DW_SPI_DMACR, 0);
	return 0;
}

static int npsc_spi_dma_init(struct dw_spi *dws)
{
	npsc_spi_dma_setup(dws);
	dws->dma_inited = 1;
	return 0;
}

static void npsc_spi_dma_exit(struct dw_spi *dws)
{
	if (dws->txchan)
		dma_release_channel(dws->txchan);

	if (dws->rxchan)
		dma_release_channel(dws->rxchan);
}

static void dw_spi_dma_done(void *arg)
{
	struct dw_spi *dws = arg;

	if (++dws->dma_chan_done == 2) {
		while(dw_readw(dws, DW_SPI_TXFLR))
			;
		dw_spi_xfer_done(dws);
		dw_writew(dws, DW_SPI_DMACR, 0);
	}
}

static int npsc_spi_dma_transfer(struct dw_spi *dws, int cs_change)
{
	struct dma_async_tx_descriptor *desc_tx, *desc_rx;
	u32 dmode = 0;

	pr_debug("%s tx=0x%x rx=0x%x len=%d\n",
		__func__, dws->tx_dma, dws->rx_dma, dws->len);

	dws->dma_chan_done = 0;

	if (dws->cur_chip->tmode == SPI_TMOD_TR ||
		dws->cur_chip->tmode == SPI_TMOD_RO) {
		desc_rx = dmaengine_prep_slave_single(
				dws->rxchan,
				dws->rx_dma,
				dws->len,
				DMA_DEV_TO_MEM,
				DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		desc_rx->callback = dw_spi_dma_done;
		desc_rx->callback_param = dws;
		dmaengine_submit(desc_rx);
		dma_async_issue_pending(dws->rxchan);
		dmode |= SPI_DMACR_RDMAE;
	}
	else {
		dws->dma_chan_done++;
	}

	if (dws->cur_chip->tmode == SPI_TMOD_TR ||
		dws->cur_chip->tmode == SPI_TMOD_TO) {

		if (!dws->tx_dma)
			dws->tx_dma = dws->rx_dma;

		desc_tx = dmaengine_prep_slave_single(
					dws->txchan,
					dws->tx_dma,
					dws->len,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		desc_tx->callback = dw_spi_dma_done;
		desc_tx->callback_param = dws;
		dmaengine_submit(desc_tx);
		dma_async_issue_pending(dws->txchan);
		dmode |= SPI_DMACR_TDMAE;
	}
	else {
		dws->dma_chan_done++;
	}

	dw_writew(dws, DW_SPI_DMACR, dmode);

	return 0;
}

static struct dw_spi_dma_ops npsc_dma_ops = {
	.dma_init	= npsc_spi_dma_init,
	.dma_exit	= npsc_spi_dma_exit,
	.dma_setup	= npsc_spi_dma_setup,
	.dma_transfer	= npsc_spi_dma_transfer,
};

int npsc_spi_dma_probe(struct platform_device *pdev, struct dw_spi *dws)
{
	struct device_node *np = pdev->dev.of_node;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	const char *string;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	/* Init tx channel */
	if(of_property_read_string(np, "dma-tx-chan", &string)) {
		return -1;
	}

	chan = dma_request_channel(mask, pl08x_filter_id, (void*)string);
	if (!chan) {
		return -1;
	}
	dws->txchan = chan;

	/* Init rx channel */
	if(of_property_read_string(np, "dma-rx-chan", &string)) {
		dma_release_channel(dws->txchan);
		return -1;
	}

	chan = dma_request_channel(mask, pl08x_filter_id, (void*)string);
	if (!chan) {
		dma_release_channel(dws->txchan);
		return -1;
	}
	dws->rxchan = chan;

	/* Init opt */
	dws->dma_ops = &npsc_dma_ops;

	return 0;
}
