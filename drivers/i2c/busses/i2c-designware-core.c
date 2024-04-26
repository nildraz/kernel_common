/*
 * Synopsys DesignWare I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 *
 * ----------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 *
 */
#include <linux/export.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/reset.h>
#include "i2c-designware-core.h"

/*
 * Registers offset
 */
#define DW_IC_CON		0x0
#define DW_IC_TAR		0x4
#define DW_IC_DATA_CMD		0x10
#define DW_IC_SS_SCL_HCNT	0x14
#define DW_IC_SS_SCL_LCNT	0x18
#define DW_IC_FS_SCL_HCNT	0x1c
#define DW_IC_FS_SCL_LCNT	0x20
#define DW_IC_INTR_STAT		0x2c
#define DW_IC_INTR_MASK		0x30
#define DW_IC_RAW_INTR_STAT	0x34
#define DW_IC_RX_TL		0x38
#define DW_IC_TX_TL		0x3c
#define DW_IC_CLR_INTR		0x40
#define DW_IC_CLR_RX_UNDER	0x44
#define DW_IC_CLR_RX_OVER	0x48
#define DW_IC_CLR_TX_OVER	0x4c
#define DW_IC_CLR_RD_REQ	0x50
#define DW_IC_CLR_TX_ABRT	0x54
#define DW_IC_CLR_RX_DONE	0x58
#define DW_IC_CLR_ACTIVITY	0x5c
#define DW_IC_CLR_STOP_DET	0x60
#define DW_IC_CLR_START_DET	0x64
#define DW_IC_CLR_GEN_CALL	0x68
#define DW_IC_ENABLE		0x6c
#define DW_IC_STATUS		0x70
#define DW_IC_TXFLR		0x74
#define DW_IC_RXFLR		0x78
#define DW_IC_SDA_HOLD          0x7c
#define DW_IC_TX_ABRT_SOURCE	0x80
#define DW_IC_ENABLE_STATUS	0x9c
#define DW_IC_COMP_PARAM_1	0xf4
#define DW_IC_COMP_TYPE		0xfc
#define DW_IC_COMP_TYPE_VALUE	0x44570140

#define DW_IC_INTR_RX_UNDER	0x001
#define DW_IC_INTR_RX_OVER	0x002
#define DW_IC_INTR_RX_FULL	0x004
#define DW_IC_INTR_TX_OVER	0x008
#define DW_IC_INTR_TX_EMPTY	0x010
#define DW_IC_INTR_RD_REQ	0x020
#define DW_IC_INTR_TX_ABRT	0x040
#define DW_IC_INTR_RX_DONE	0x080
#define DW_IC_INTR_ACTIVITY	0x100
#define DW_IC_INTR_STOP_DET	0x200
#define DW_IC_INTR_START_DET	0x400
#define DW_IC_INTR_GEN_CALL	0x800

#define DW_IC_INTR_DEFAULT_MASK		(DW_IC_INTR_RX_OVER  | \
					 DW_IC_INTR_RX_FULL | \
					 DW_IC_INTR_TX_EMPTY | \
					 DW_IC_INTR_TX_ABRT | \
					 DW_IC_INTR_STOP_DET)

#define DW_IC_STATUS_ACTIVITY	0x1

#define DW_IC_ERR_TX_ABRT	0x1
#define	DW_IC_ERR_RX_OVER	0x2

/*
 * status codes
 */
#define STATUS_IDLE			0x0
#define STATUS_WRITE_IN_PROGRESS	0x1
#define STATUS_READ_IN_PROGRESS		0x2

#define TIMEOUT			20 /* ms */

/*
 * hardware abort codes from the DW_IC_TX_ABRT_SOURCE register
 *
 * only expected abort codes are listed here
 * refer to the datasheet for the full list
 */
#define ABRT_7B_ADDR_NOACK	0
#define ABRT_10ADDR1_NOACK	1
#define ABRT_10ADDR2_NOACK	2
#define ABRT_TXDATA_NOACK	3
#define ABRT_GCALL_NOACK	4
#define ABRT_GCALL_READ		5
#define ABRT_SBYTE_ACKDET	7
#define ABRT_SBYTE_NORSTRT	9
#define ABRT_10B_RD_NORSTRT	10
#define ABRT_MASTER_DIS		11
#define ARB_LOST		12

#define DW_IC_TX_ABRT_7B_ADDR_NOACK	(1UL << ABRT_7B_ADDR_NOACK)
#define DW_IC_TX_ABRT_10ADDR1_NOACK	(1UL << ABRT_10ADDR1_NOACK)
#define DW_IC_TX_ABRT_10ADDR2_NOACK	(1UL << ABRT_10ADDR2_NOACK)
#define DW_IC_TX_ABRT_TXDATA_NOACK	(1UL << ABRT_TXDATA_NOACK)
#define DW_IC_TX_ABRT_GCALL_NOACK	(1UL << ABRT_GCALL_NOACK)
#define DW_IC_TX_ABRT_GCALL_READ	(1UL << ABRT_GCALL_READ)
#define DW_IC_TX_ABRT_SBYTE_ACKDET	(1UL << ABRT_SBYTE_ACKDET)
#define DW_IC_TX_ABRT_SBYTE_NORSTRT	(1UL << ABRT_SBYTE_NORSTRT)
#define DW_IC_TX_ABRT_10B_RD_NORSTRT	(1UL << ABRT_10B_RD_NORSTRT)
#define DW_IC_TX_ABRT_MASTER_DIS	(1UL << ABRT_MASTER_DIS)
#define DW_IC_TX_ARB_LOST		(1UL << ARB_LOST)

#define DW_IC_TX_ABRT_NOACK		(DW_IC_TX_ABRT_7B_ADDR_NOACK | \
					 DW_IC_TX_ABRT_10ADDR1_NOACK | \
					 DW_IC_TX_ABRT_10ADDR2_NOACK | \
					 DW_IC_TX_ABRT_TXDATA_NOACK | \
					 DW_IC_TX_ABRT_GCALL_NOACK)


static char *abort_sources[] = {
	[ABRT_7B_ADDR_NOACK] =
		"slave address not acknowledged (7bit mode)",
	[ABRT_10ADDR1_NOACK] =
		"first address byte not acknowledged (10bit mode)",
	[ABRT_10ADDR2_NOACK] =
		"second address byte not acknowledged (10bit mode)",
	[ABRT_TXDATA_NOACK] =
		"data not acknowledged",
	[ABRT_GCALL_NOACK] =
		"no acknowledgement for a general call",
	[ABRT_GCALL_READ] =
		"read after general call",
	[ABRT_SBYTE_ACKDET] =
		"start byte acknowledged",
	[ABRT_SBYTE_NORSTRT] =
		"trying to send start byte when restart is disabled",
	[ABRT_10B_RD_NORSTRT] =
		"trying to read when restart is disabled (10bit mode)",
	[ABRT_MASTER_DIS] =
		"trying to use disabled adapter",
	[ARB_LOST] =
		"lost arbitration",
};

u32 dw_readl(struct dw_i2c_dev *dev, int offset)
{
	u32 value;

	if (dev->accessor_flags & ACCESS_16BIT)
		value = readw(dev->base + offset) |
			(readw(dev->base + offset + 2) << 16);
	else
		value = readl(dev->base + offset);

	if (dev->accessor_flags & ACCESS_SWAP)
		return swab32(value);
	else
		return value;
}

void dw_writel(struct dw_i2c_dev *dev, u32 b, int offset)
{
	if (dev->accessor_flags & ACCESS_SWAP)
		b = swab32(b);

	if (dev->accessor_flags & ACCESS_16BIT) {
		writew((u16)b, dev->base + offset);
		writew((u16)(b >> 16), dev->base + offset + 2);
	} else {
		writel(b, dev->base + offset);
	}
}

static u32
i2c_dw_scl_hcnt(u32 ic_clk, u32 tSYMBOL, u32 tf, int cond, int offset)
{
	/*
	 * DesignWare I2C core doesn't seem to have solid strategy to meet
	 * the tHD;STA timing spec.  Configuring _HCNT based on tHIGH spec
	 * will result in violation of the tHD;STA spec.
	 */
	if (cond)
		/*
		 * Conditional expression:
		 *
		 *   IC_[FS]S_SCL_HCNT + (1+4+3) >= IC_CLK * tHIGH
		 *
		 * This is based on the DW manuals, and represents an ideal
		 * configuration.  The resulting I2C bus speed will be
		 * faster than any of the others.
		 *
		 * If your hardware is free from tHD;STA issue, try this one.
		 */
		return (ic_clk * tSYMBOL + 5000) / 10000 - 8 + offset;
	else
		/*
		 * Conditional expression:
		 *
		 *   IC_[FS]S_SCL_HCNT + 3 >= IC_CLK * (tHD;STA + tf)
		 *
		 * This is just experimental rule; the tHD;STA period turned
		 * out to be proportinal to (_HCNT + 3).  With this setting,
		 * we could meet both tHIGH and tHD;STA timing specs.
		 *
		 * If unsure, you'd better to take this alternative.
		 *
		 * The reason why we need to take into account "tf" here,
		 * is the same as described in i2c_dw_scl_lcnt().
		 */
		return (ic_clk * (tSYMBOL + tf) + 5000) / 10000 - 3 + offset;
}

static u32 i2c_dw_scl_lcnt(u32 ic_clk, u32 tLOW, u32 tf, int offset)
{
	/*
	 * Conditional expression:
	 *
	 *   IC_[FS]S_SCL_LCNT + 1 >= IC_CLK * (tLOW + tf)
	 *
	 * DW I2C core starts counting the SCL CNTs for the LOW period
	 * of the SCL clock (tLOW) as soon as it pulls the SCL line.
	 * In order to meet the tLOW timing spec, we need to take into
	 * account the fall time of SCL signal (tf).  Default tf value
	 * should be 0.3 us, for safety.
	 */
	return ((ic_clk * (tLOW + tf) + 5000) / 10000) - 1 + offset;
}

static void __i2c_dw_enable(struct dw_i2c_dev *dev, bool enable)
{
	int timeout = 100;

	do {
		dw_writel(dev, enable, DW_IC_ENABLE);
		if ((dw_readl(dev, DW_IC_ENABLE_STATUS) & 1) == enable)
			return;

		/*
		 * Wait 10 times the signaling period of the highest I2C
		 * transfer supported by the driver (for 400KHz this is
		 * 25us) as described in the DesignWare I2C databook.
		 */
		usleep_range(25, 250);
	} while (timeout--);

	dev_warn(dev->dev, "timeout in %sabling adapter\n",
		 enable ? "en" : "dis");
}

/**
 * i2c_dw_init() - initialize the designware i2c master hardware
 * @dev: device private data
 *
 * This functions configures and enables the I2C master.
 * This function is called during I2C init function, and in case of timeout at
 * run time.
 */
int i2c_dw_init(struct dw_i2c_dev *dev)
{
	u32 input_clock_khz;
	u32 hcnt, lcnt;
	u32 reg;
	u32 cycle;

	input_clock_khz = dev->get_clk_rate_khz(dev);

	reg = dw_readl(dev, DW_IC_COMP_TYPE);
	if (reg == ___constant_swab32(DW_IC_COMP_TYPE_VALUE)) {
		/* Configure register endianess access */
		dev->accessor_flags |= ACCESS_SWAP;
	} else if (reg == (DW_IC_COMP_TYPE_VALUE & 0x0000ffff)) {
		/* Configure register access mode 16bit */
		dev->accessor_flags |= ACCESS_16BIT;
	} else if (reg != DW_IC_COMP_TYPE_VALUE) {
		dev_err(dev->dev, "Unknown Synopsys component type: "
			"0x%08x\n", reg);
		return -ENODEV;
	}

	/* Disable the adapter */
	__i2c_dw_enable(dev, false);

	/* set standard and fast speed deviders for high/low periods */

	/* Standard-mode */
	hcnt = i2c_dw_scl_hcnt(input_clock_khz,
				40,	/* tHD;STA = tHIGH = 4.0 us */
				18,	/* tf = 1.8 us */
				0,	/* 0: DW default, 1: Ideal */
				0);	/* No offset */
	lcnt = i2c_dw_scl_lcnt(input_clock_khz,
				47,	/* tLOW = 4.7 us */
				6,	/* tf = 0.6 us */
				0);	/* No offset */
	dw_writel(dev, hcnt, DW_IC_SS_SCL_HCNT);
	dw_writel(dev, lcnt, DW_IC_SS_SCL_LCNT);
	dev_dbg(dev->dev, "Standard-mode HCNT:LCNT = %d:%d\n", hcnt, lcnt);

	/* Fast-mode */
	hcnt = i2c_dw_scl_hcnt(input_clock_khz,
				6,	/* tHD;STA = tHIGH = 0.6 us */
				3,	/* tf = 0.3 us */
				0,	/* 0: DW default, 1: Ideal */
				0);	/* No offset */
	lcnt = i2c_dw_scl_lcnt(input_clock_khz,
				13,	/* tLOW = 1.3 us */
				3,	/* tf = 0.3 us */
				0);	/* No offset */
	dw_writel(dev, hcnt, DW_IC_FS_SCL_HCNT);
	dw_writel(dev, lcnt, DW_IC_FS_SCL_LCNT);
	dev_dbg(dev->dev, "Fast-mode HCNT:LCNT = %d:%d\n", hcnt, lcnt);

	/* Configure Tx/Rx FIFO threshold levels */
	dw_writel(dev, dev->tx_fifo_depth - 1, DW_IC_TX_TL);
	dw_writel(dev, 0, DW_IC_RX_TL);

	/* Calculate cycle time, the unit is ns */
	cycle = 1000000 / input_clock_khz;
	/* Set sda hold time as 300 ns */
	dw_writel(dev, 300/cycle, DW_IC_SDA_HOLD);

	/* configure the i2c master */
	dw_writel(dev, dev->master_cfg , DW_IC_CON);
	return 0;
}
EXPORT_SYMBOL_GPL(i2c_dw_init);

/*
 * Waiting for bus not busy
 */
static int i2c_dw_wait_bus_not_busy(struct dw_i2c_dev *dev)
{
	int timeout = TIMEOUT;

	while (dw_readl(dev, DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY) {
		if (timeout <= 0) {
			dev_warn(dev->dev, "timeout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		timeout--;
		usleep_range(1000, 1100);
	}

	return 0;
}

static void i2c_dw_xfer_init(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 ic_con;

	/* Disable the adapter */
	__i2c_dw_enable(dev, false);

	/* set the slave (target) address */
	dw_writel(dev, msgs[dev->msg_write_idx].addr, DW_IC_TAR);

	/* if the slave address is ten bit address, enable 10BITADDR */
	ic_con = dw_readl(dev, DW_IC_CON);
	if (msgs[dev->msg_write_idx].flags & I2C_M_TEN)
		ic_con |= DW_IC_CON_10BITADDR_MASTER;
	else
		ic_con &= ~DW_IC_CON_10BITADDR_MASTER;
	dw_writel(dev, ic_con, DW_IC_CON);

	/**
	 *diable all interrupts (HW issue)
	 */
	 i2c_dw_disable_int(dev);
	/* Enable the adapter */
	__i2c_dw_enable(dev, true);

	/* Clear and enable interrupts */
	i2c_dw_clear_int(dev);
	dw_writel(dev, DW_IC_INTR_DEFAULT_MASK, DW_IC_INTR_MASK);
}

/*
 * Initiate (and continue) low level master read/write transaction.
 * This function is only called from i2c_dw_isr, and pumping i2c_msg
 * messages into the tx buffer.  Even if the size of i2c_msg data is
 * longer than the size of the tx buffer, it handles everything.
 */
static void
i2c_dw_xfer_msg(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	u32 intr_mask;
	int tx_limit, rx_limit,rx_fifo_count;
	u32 addr = msgs[dev->msg_write_idx].addr;
	u32 buf_len = dev->tx_buf_len;
	u8 *buf = dev->tx_buf;

	intr_mask = DW_IC_INTR_DEFAULT_MASK;

	for (; dev->msg_write_idx < dev->msgs_num; dev->msg_write_idx++) {
		/*
		 * if target address has changed, we need to
		 * reprogram the target address in the i2c
		 * adapter when we are done with this transfer
		 */
		if (msgs[dev->msg_write_idx].addr != addr) {
			dev_err(dev->dev,
				"%s: invalid target address\n", __func__);
			dev->msg_err = -EINVAL;
			break;
		}

		if (msgs[dev->msg_write_idx].len == 0) {
			dev_err(dev->dev,
				"%s: invalid message length\n", __func__);
			dev->msg_err = -EINVAL;
			break;
		}

		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			/* new i2c_msg */
			buf = msgs[dev->msg_write_idx].buf;
			buf_len = msgs[dev->msg_write_idx].len;
		}

		tx_limit = dev->tx_fifo_depth - dw_readl(dev, DW_IC_TXFLR);

		/* avoid rx buffer overrun */
		if(msgs[dev->msg_write_idx].flags & I2C_M_RD){
			rx_fifo_count  = dw_readl(dev,DW_IC_RXFLR);
			rx_limit = dev->rx_fifo_depth - rx_fifo_count;

			if(dev->rx_outstanding > rx_fifo_count){
				rx_fifo_count = dev->rx_outstanding - rx_fifo_count;
				rx_limit -= rx_fifo_count;
				rx_limit = rx_limit < 0 ? 0 : rx_limit;
			}

			tx_limit = tx_limit < rx_limit ? tx_limit : rx_limit;
		}

		while (buf_len > 0 && tx_limit > 0) {
			u32 cmd = 0;

			/*
			 * If IC_EMPTYFIFO_HOLD_MASTER_EN is set we must
			 * manually set the stop bit. However, it cannot be
			 * detected from the registers so we set it always
			 * when writing/reading the last byte.
			 */
			if (dev->msg_write_idx == dev->msgs_num - 1 &&
			    buf_len == 1)
				cmd |= BIT(9);

			if (msgs[dev->msg_write_idx].flags & I2C_M_RD) {

				/* avoid rx buffer overrun */
				//if (rx_limit - dev->rx_outstanding <= 0)
				//	break;

				dw_writel(dev, cmd | 0x100, DW_IC_DATA_CMD);
				dev->rx_outstanding++;
			} else
				dw_writel(dev, cmd | *buf++, DW_IC_DATA_CMD);
			tx_limit--; buf_len--;
		}

		dev->tx_buf = buf;
		dev->tx_buf_len = buf_len;

		if (buf_len > 0) {
			/* more bytes to be written */
			dev->status |= STATUS_WRITE_IN_PROGRESS;
			break;
		} else
			dev->status &= ~STATUS_WRITE_IN_PROGRESS;
	}

	/*
	 * If i2c_msg index search is completed, we don't need TX_EMPTY
	 * interrupt any more.
	 */
	if (dev->msg_write_idx == dev->msgs_num) {
#ifdef CONFIG_I2C_DESIGNWARE_TX_FIFO_EMPTY_HOLD_MASTER
		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			while (dw_readl(dev, DW_IC_TXFLR) == dev->tx_fifo_depth)
				udelay(10);
		}
#endif
		intr_mask &= ~DW_IC_INTR_TX_EMPTY;
	}
	if (dev->msg_err)
		intr_mask = 0;

	dw_writel(dev, intr_mask,  DW_IC_INTR_MASK);
}

static void
i2c_dw_read(struct dw_i2c_dev *dev)
{
	struct i2c_msg *msgs = dev->msgs;
	int rx_valid;

	for (; dev->msg_read_idx < dev->msgs_num; dev->msg_read_idx++) {
		u32 len;
		u8 *buf;

		if (!(msgs[dev->msg_read_idx].flags & I2C_M_RD))
			continue;

		if (!(dev->status & STATUS_READ_IN_PROGRESS)) {
			len = msgs[dev->msg_read_idx].len;
			buf = msgs[dev->msg_read_idx].buf;
		} else {
			len = dev->rx_buf_len;
			buf = dev->rx_buf;
		}

		rx_valid = dw_readl(dev, DW_IC_RXFLR);

		for (; len > 0 && rx_valid > 0; len--, rx_valid--) {
			*buf++ = dw_readl(dev, DW_IC_DATA_CMD);
			dev->rx_outstanding--;
		}

		if (len > 0) {
			dev->status |= STATUS_READ_IN_PROGRESS;
			dev->rx_buf_len = len;
			dev->rx_buf = buf;
			return;
		} else
			dev->status &= ~STATUS_READ_IN_PROGRESS;
	}
}

static int i2c_dw_handle_tx_abort(struct dw_i2c_dev *dev)
{
	unsigned long abort_source = dev->abort_source;
	int i;

	if (abort_source & DW_IC_TX_ABRT_NOACK) {
		for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
			dev_err(dev->dev,
				"%s: device:0x%x err:%s\n", __func__, dev->msgs[0].addr, abort_sources[i]);
		return -EREMOTEIO;
	}

	for_each_set_bit(i, &abort_source, ARRAY_SIZE(abort_sources))
		dev_err(dev->dev, "%s: %s\n", __func__, abort_sources[i]);

	if (abort_source & DW_IC_TX_ARB_LOST)
		return -EAGAIN;
	else if (abort_source & DW_IC_TX_ABRT_GCALL_READ)
		return -EINVAL; /* wrong msgs[] data */
	else
		return -EIO;
}

extern void devm_reset_control_put(struct reset_control *rstc);
/*
 * Prepare controller for a transaction and call i2c_dw_xfer_msg
 */
int
i2c_dw_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	struct reset_control *rst;
	int ret;

	dev_dbg(dev->dev, "%s: msgs: %d\n", __func__, num);

	mutex_lock(&dev->lock);
	pm_runtime_get_sync(dev->dev);
	if (dev->suspend_flag == 1) {
		ret = -ETIMEDOUT;
		goto err;
	}

	INIT_COMPLETION(dev->cmd_complete);
	dev->msgs = msgs;
	dev->msgs_num = num;
	dev->cmd_err = 0;
	dev->msg_write_idx = 0;
	dev->msg_read_idx = 0;
	dev->msg_err = 0;
	dev->status = STATUS_IDLE;
	dev->abort_source = 0;
	dev->rx_outstanding = 0;
#ifdef CONFIG_RESET_CONTROLLER
	rst = devm_reset_control_get(dev->dev, NULL);
	if (IS_ERR(rst) && PTR_ERR(rst) == -EPROBE_DEFER) {
		dev_err(dev->dev, "can not get reset ops\n");
		ret = -ETIMEDOUT;
		goto err;
	}
#endif
	ret = i2c_dw_wait_bus_not_busy(dev);
	if (ret < 0)
		goto done;
	 /* add workround for time out bug */
#ifdef	CONFIG_RESET_CONTROLLER
	reset_control_assert(rst);
	reset_control_deassert(rst);
	udelay(10);
	i2c_dw_init(dev);
#endif

	/* start the transfers */
	i2c_dw_xfer_init(dev);

	/* wait for tx to complete */
	/*ret = wait_for_completion_interruptible_timeout(&dev->cmd_complete, HZ);*/
	ret = wait_for_completion_timeout(&dev->cmd_complete, HZ);
	if (ret == 0) {
		dev_err(dev->dev, "controller timed out\n");
#ifdef CONFIG_RESET_CONTROLLER
		reset_control_assert(rst);
		reset_control_deassert(rst);
#endif
		i2c_dw_init(dev);
		ret = -ETIMEDOUT;
		goto done;
	} else if (ret < 0)
		goto done;

	if (dev->msg_err) {
		ret = dev->msg_err;
		goto done;
	}

	/* no error */
	if (likely(!dev->cmd_err)) {
		/* Disable the adapter */
		__i2c_dw_enable(dev, false);
		ret = num;
		goto done;
	}

	/* We have an error */
	if (dev->cmd_err &  DW_IC_ERR_TX_ABRT) {
		ret = i2c_dw_handle_tx_abort(dev);
		goto done;
	}
	if(dev->cmd_err & DW_IC_ERR_RX_OVER){
		dev_err(dev->dev,"receive buffer goes above RX_FIFO_DEPTH\n");
		ret = -EAGAIN;
		goto done;
	}
	ret = -EIO;

done:
#ifdef CONFIG_RESET_CONTROLLER
	devm_reset_control_put(rst);
#endif
err:
	pm_runtime_mark_last_busy(dev->dev);
	pm_runtime_put_autosuspend(dev->dev);
	mutex_unlock(&dev->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_dw_xfer);

u32 i2c_dw_func(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	return dev->functionality;
}
EXPORT_SYMBOL_GPL(i2c_dw_func);

static u32 i2c_dw_read_clear_intrbits(struct dw_i2c_dev *dev)
{
	u32 stat;

	/*
	 * The IC_INTR_STAT register just indicates "enabled" interrupts.
	 * Ths unmasked raw version of interrupt status bits are available
	 * in the IC_RAW_INTR_STAT register.
	 *
	 * That is,
	 *   stat = dw_readl(IC_INTR_STAT);
	 * equals to,
	 *   stat = dw_readl(IC_RAW_INTR_STAT) & dw_readl(IC_INTR_MASK);
	 *
	 * The raw version might be useful for debugging purposes.
	 */
	stat = dw_readl(dev, DW_IC_INTR_STAT);

	/*
	 * Do not use the IC_CLR_INTR register to clear interrupts, or
	 * you'll miss some interrupts, triggered during the period from
	 * dw_readl(IC_INTR_STAT) to dw_readl(IC_CLR_INTR).
	 *
	 * Instead, use the separately-prepared IC_CLR_* registers.
	 */
	if (stat & DW_IC_INTR_RX_UNDER)
		dw_readl(dev, DW_IC_CLR_RX_UNDER);
	if (stat & DW_IC_INTR_RX_OVER)
		dw_readl(dev, DW_IC_CLR_RX_OVER);
	if (stat & DW_IC_INTR_TX_OVER)
		dw_readl(dev, DW_IC_CLR_TX_OVER);
	if (stat & DW_IC_INTR_RD_REQ)
		dw_readl(dev, DW_IC_CLR_RD_REQ);
	if (stat & DW_IC_INTR_TX_ABRT) {
		/*
		 * The IC_TX_ABRT_SOURCE register is cleared whenever
		 * the IC_CLR_TX_ABRT is read.  Preserve it beforehand.
		 */
		dev->abort_source = dw_readl(dev, DW_IC_TX_ABRT_SOURCE);
		dw_readl(dev, DW_IC_CLR_TX_ABRT);
	}
	if (stat & DW_IC_INTR_RX_DONE)
		dw_readl(dev, DW_IC_CLR_RX_DONE);
	if (stat & DW_IC_INTR_ACTIVITY)
		dw_readl(dev, DW_IC_CLR_ACTIVITY);
	if (stat & DW_IC_INTR_STOP_DET)
		dw_readl(dev, DW_IC_CLR_STOP_DET);
	if (stat & DW_IC_INTR_START_DET)
		dw_readl(dev, DW_IC_CLR_START_DET);
	if (stat & DW_IC_INTR_GEN_CALL)
		dw_readl(dev, DW_IC_CLR_GEN_CALL);

	return stat;
}

/*
 * Interrupt service routine. This gets called whenever an I2C interrupt
 * occurs.
 */
irqreturn_t i2c_dw_isr(int this_irq, void *dev_id)
{
	struct dw_i2c_dev *dev = dev_id;
	u32 stat, enabled;

	enabled = dw_readl(dev, DW_IC_ENABLE);
	stat = dw_readl(dev, DW_IC_RAW_INTR_STAT);
	dev_dbg(dev->dev, "%s:  %s enabled= 0x%x stat=0x%x\n", __func__,
		dev->adapter.name, enabled, stat);
	if (!enabled || !(stat & ~DW_IC_INTR_ACTIVITY))
		return IRQ_NONE;

	stat = i2c_dw_read_clear_intrbits(dev);

	if (stat & DW_IC_INTR_TX_ABRT) {
		dev->cmd_err |= DW_IC_ERR_TX_ABRT;
		dev->status = STATUS_IDLE;

		/*
		 * Anytime TX_ABRT is set, the contents of the tx/rx
		 * buffers are flushed.  Make sure to skip them.
		 */
		dw_writel(dev, 0, DW_IC_INTR_MASK);
		goto tx_aborted;
	}
	if(stat & DW_IC_INTR_RX_OVER){
		dev->cmd_err |= DW_IC_ERR_RX_OVER;
		dev->status = STATUS_IDLE;
		dw_writel(dev,0,DW_IC_INTR_MASK);
		goto tx_aborted;
	}

	if (stat & DW_IC_INTR_RX_FULL)
		i2c_dw_read(dev);

	if (stat & DW_IC_INTR_TX_EMPTY)
		i2c_dw_xfer_msg(dev);

	/*
	 * No need to modify or disable the interrupt mask here.
	 * i2c_dw_xfer_msg() will take care of it according to
	 * the current transmit status.
	 */

tx_aborted:
	if ((stat & (DW_IC_INTR_TX_ABRT | DW_IC_INTR_STOP_DET | DW_IC_INTR_RX_OVER)) || dev->msg_err)
		complete(&dev->cmd_complete);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(i2c_dw_isr);

void i2c_dw_enable(struct dw_i2c_dev *dev)
{
       /* Enable the adapter */
	__i2c_dw_enable(dev, true);
}
EXPORT_SYMBOL_GPL(i2c_dw_enable);

u32 i2c_dw_is_enabled(struct dw_i2c_dev *dev)
{
	return dw_readl(dev, DW_IC_ENABLE);
}
EXPORT_SYMBOL_GPL(i2c_dw_is_enabled);

void i2c_dw_disable(struct dw_i2c_dev *dev)
{
	/* Disable controller */
	__i2c_dw_enable(dev, false);

	/* Disable all interupts */
	dw_writel(dev, 0, DW_IC_INTR_MASK);
	dw_readl(dev, DW_IC_CLR_INTR);
}
EXPORT_SYMBOL_GPL(i2c_dw_disable);

void i2c_dw_clear_int(struct dw_i2c_dev *dev)
{
	dw_readl(dev, DW_IC_CLR_INTR);
}
EXPORT_SYMBOL_GPL(i2c_dw_clear_int);

void i2c_dw_disable_int(struct dw_i2c_dev *dev)
{
	dw_writel(dev, 0, DW_IC_INTR_MASK);
}
EXPORT_SYMBOL_GPL(i2c_dw_disable_int);

u32 i2c_dw_read_comp_param(struct dw_i2c_dev *dev)
{
	return dw_readl(dev, DW_IC_COMP_PARAM_1);
}
EXPORT_SYMBOL_GPL(i2c_dw_read_comp_param);


void dw_i2c_send_bytes(struct dw_i2c_dev *i2c_dev, unsigned char *out_buf, unsigned int len)
{
	unsigned int i = 1;
	if (!i2c_dev) {
		pr_err("%s i2c_dev empty!\n", __func__);
		return ;
	}

	writel(out_buf[0], i2c_dev->base + DW_IC_DATA_CMD);
	while ((readl(i2c_dev->base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_START_DET) != DW_IC_INTR_START_DET)
		;

	while (i < len && (readl(i2c_dev->base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_STOP_DET) != DW_IC_INTR_STOP_DET) {
		if ((readl(i2c_dev->base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_TX_EMPTY) == DW_IC_INTR_TX_EMPTY) {
			if (i == len - 1)
				writel(0x200 | out_buf[i], i2c_dev->base + DW_IC_DATA_CMD);
			else
				writel(out_buf[i], i2c_dev->base + DW_IC_DATA_CMD);
			i++;
		}
	}

	while ((readl(i2c_dev->base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_STOP_DET) != DW_IC_INTR_STOP_DET)
		;

	readl(i2c_dev->base + DW_IC_CLR_INTR);
	readl(i2c_dev->base + DW_IC_CLR_RX_UNDER);
	readl(i2c_dev->base + DW_IC_CLR_RX_OVER);
	readl(i2c_dev->base + DW_IC_CLR_TX_OVER);
	readl(i2c_dev->base + DW_IC_CLR_RD_REQ);
	readl(i2c_dev->base + DW_IC_CLR_TX_ABRT);
	readl(i2c_dev->base + DW_IC_CLR_RX_DONE);
	readl(i2c_dev->base + DW_IC_CLR_ACTIVITY);
	readl(i2c_dev->base + DW_IC_CLR_STOP_DET);
	readl(i2c_dev->base + DW_IC_CLR_START_DET);
	readl(i2c_dev->base + DW_IC_CLR_GEN_CALL);
}

void dw_i2c_smbus_read(struct dw_i2c_dev *i2c_dev, unsigned char *out_buf, unsigned int wlen, unsigned char *rbuf, unsigned rlen)
{
	unsigned int i = 1, j = 0;
	void __iomem *i2c_base ;
	int val = 0x55aa;
	if (!i2c_dev) {
		pr_err("%s i2c_dev empty!\n", __func__);
		return ;
	}
	i2c_base = i2c_dev->base;

	writel(out_buf[0], i2c_base + DW_IC_DATA_CMD);
	while ((readl(i2c_base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_START_DET) != DW_IC_INTR_START_DET) {
	};

	while (i < wlen) {
		if ((readl(i2c_base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_TX_EMPTY) == DW_IC_INTR_TX_EMPTY) {
			writel(out_buf[i], i2c_base + DW_IC_DATA_CMD);
			i++;
		}
	}

	writel(0x100, i2c_base + DW_IC_DATA_CMD);
	i = 0;
	j = 0;
	while (i < rlen && (readl(i2c_base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_STOP_DET) != DW_IC_INTR_STOP_DET) {

		if (j < rlen && (readl(i2c_base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_TX_EMPTY) == DW_IC_INTR_TX_EMPTY) {
			if (j == rlen-1)
				writel(0x300, i2c_base + DW_IC_DATA_CMD);
			else
				writel(0x100, i2c_base + DW_IC_DATA_CMD);
			j++;
		}
		val = readl(i2c_base + DW_IC_STATUS);
		if (val & 0x08) { /* 1: Receive FIFO is not empty */
			rbuf[i++] = (unsigned char)readl(i2c_base + DW_IC_DATA_CMD);
		}
	}

	while ((readl(i2c_base + DW_IC_RAW_INTR_STAT) & DW_IC_INTR_STOP_DET) != DW_IC_INTR_STOP_DET) {
	}
	readl(i2c_base + DW_IC_CLR_INTR);
	readl(i2c_base + DW_IC_CLR_RX_UNDER);
	readl(i2c_base + DW_IC_CLR_RX_OVER);
	readl(i2c_base + DW_IC_CLR_TX_OVER);
	readl(i2c_base + DW_IC_CLR_RD_REQ);
	readl(i2c_base + DW_IC_CLR_TX_ABRT);
	readl(i2c_base + DW_IC_CLR_RX_DONE);
	readl(i2c_base + DW_IC_CLR_ACTIVITY);
	readl(i2c_base + DW_IC_CLR_STOP_DET);
	readl(i2c_base + DW_IC_CLR_START_DET);
	readl(i2c_base + DW_IC_CLR_GEN_CALL);
}

extern int dw_i2c_runtime_resume(struct device *dev);
void dw_i2c_master_init(struct dw_i2c_dev *i2c_dev, unsigned char slv_addr)
{
	unsigned int ic_con = 0;
	static u8 flag = 0;
	if (flag == 0) {
#ifdef CONFIG_PM_RUNTIME
		dw_i2c_runtime_resume(i2c_dev->dev);
		pr_err("set i2c clk and pinctrl\n");
#endif
		flag = 1;
	}
#if 0
	struct reset_control *rst;
	int ret;
#endif

	if (!i2c_dev) {
		pr_err("%s i2c_dev empty!\n", __func__);
		return ;
	}
#if 0 /* add workround for time out bug */

	rst = devm_reset_control_get(i2c_dev->dev, NULL);
	if (IS_ERR(rst) && PTR_ERR(rst) == -EPROBE_DEFER) {
		dev_err(i2c_dev->dev, "can not get reset ops\n");
		return;
	}
	reset_control_assert(rst);
	reset_control_deassert(rst);
	udelay(10);
	i2c_dw_init(i2c_dev);
#endif

	writel(0, i2c_dev->base + DW_IC_ENABLE);
	ic_con = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE |
		DW_IC_CON_RESTART_EN | DW_IC_CON_SPEED_STD;

	writel(ic_con , i2c_dev->base + DW_IC_CON);
	writel(slv_addr, i2c_dev->base + DW_IC_TAR);
	writel(1, i2c_dev->base + DW_IC_ENABLE);
}

EXPORT_SYMBOL(dw_i2c_master_init);
EXPORT_SYMBOL(dw_i2c_send_bytes);
EXPORT_SYMBOL(dw_i2c_smbus_read);


MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter core");
MODULE_LICENSE("GPL");
