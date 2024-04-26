/*
 * npsc 8250 driver.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>

#include "8250.h"

/* Offsets for the specific registers */
#define NPSC_UART_USR	0x1f /* UART Status Register */
#define NPSC_UART_CPR	0xf4 /* Component Parameter Register */
#define NPSC_UART_UCV	0xf8 /* UART Component Version */

/* Component Parameter Register bits */
#define NPSC_UART_CPR_ABP_DATA_WIDTH	(3 << 0)
#define NPSC_UART_CPR_AFCE_MODE		(1 << 4)
#define NPSC_UART_CPR_THRE_MODE		(1 << 5)
#define NPSC_UART_CPR_SIR_MODE		(1 << 6)
#define NPSC_UART_CPR_SIR_LP_MODE		(1 << 7)
#define NPSC_UART_CPR_ADDITIONAL_FEATURES	(1 << 8)
#define NPSC_UART_CPR_FIFO_ACCESS		(1 << 9)
#define NPSC_UART_CPR_FIFO_STAT		(1 << 10)
#define NPSC_UART_CPR_SHADOW		(1 << 11)
#define NPSC_UART_CPR_ENCODED_PARMS	(1 << 12)
#define NPSC_UART_CPR_DMA_EXTRA		(1 << 13)
#define NPSC_UART_CPR_FIFO_MODE		(0xff << 16)
/* Helper for fifo size calculation */
#define NPSC_UART_CPR_FIFO_SIZE(a)	(((a >> 16) & 0xff) * 16)


struct npsc_data {
	int		last_lcr;
	int		line;
	struct clk	*clk;
};

static void npsc_serial_out(struct uart_port *p, int offset, int value)
{
	struct npsc_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writeb(value, p->membase + offset);
}

static unsigned int npsc_serial_in(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readb(p->membase + offset);
}

static void npsc_serial_out32(struct uart_port *p, int offset, int value)
{
	struct npsc_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writel(value, p->membase + offset);
}

static unsigned int npsc_serial_in32(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readl(p->membase + offset);
}

static int npsc_handle_irq(struct uart_port *p)
{
	struct npsc_data *d = p->private_data;
	unsigned int iir = p->serial_in(p, UART_IIR);

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/* Clear the USR and write the LCR again. */
		(void)p->serial_in(p, NPSC_UART_USR);
		p->serial_out(p, UART_LCR, d->last_lcr);

		return 1;
	}

	return 0;
}

static void
npsc_do_pm(struct uart_port *port, unsigned int state, unsigned int old)
{
	if (!state)
		pm_runtime_get_sync(port->dev);

	serial8250_do_pm(port, state, old);

	if (state)
		pm_runtime_put_sync_suspend(port->dev);
}

static int npsc_probe_of(struct uart_port *p)
{
	struct device_node	*np = p->dev->of_node;
	u32			val;

	if (!of_property_read_u32(np, "reg-io-width", &val)) {
		switch (val) {
		case 1:
			break;
		case 4:
			p->iotype = UPIO_MEM32;
			p->serial_in = npsc_serial_in32;
			p->serial_out = npsc_serial_out32;
			break;
		default:
			dev_err(p->dev, "unsupported reg-io-width (%u)\n", val);
			return -EINVAL;
		}
	}

	if (!of_property_read_u32(np, "reg-shift", &val))
		p->regshift = val;

	/* clock got configured through clk api, all done */
	if (p->uartclk)
		return 0;

	/* try to find out clock frequency from DT as fallback */
	if (of_property_read_u32(np, "clock-frequency", &val)) {
		dev_err(p->dev, "clk or clock-frequency not defined\n");
		return -EINVAL;
	}
	p->uartclk = val;

	return 0;
}

#ifdef CONFIG_ACPI
static int npsc_probe_acpi(struct uart_8250_port *up)
{
	const struct acpi_device_id *id;
	struct uart_port *p = &up->port;

	id = acpi_match_device(p->dev->driver->acpi_match_table, p->dev);
	if (!id)
		return -ENODEV;

	p->iotype = UPIO_MEM32;
	p->serial_in = npsc_serial_in32;
	p->serial_out = npsc_serial_out32;
	p->regshift = 2;

	if (!p->uartclk)
		p->uartclk = (unsigned int)id->driver_data;

	up->dma = devm_kzalloc(p->dev, sizeof(*up->dma), GFP_KERNEL);
	if (!up->dma)
		return -ENOMEM;

	up->dma->rxconf.src_maxburst = p->fifosize / 4;
	up->dma->txconf.dst_maxburst = p->fifosize / 4;

	return 0;
}
#else
static inline int npsc_probe_acpi(struct uart_8250_port *up)
{
	return -ENODEV;
}
#endif /* CONFIG_ACPI */

static void npsc_setup_port(struct uart_8250_port *up)
{
	struct uart_port	*p = &up->port;
	u32			reg = readl(p->membase + NPSC_UART_UCV);

	/*
	 * If the Component Version Register returns zero, we know that
	 * ADDITIONAL_FEATURES are not enabled. No need to go any further.
	 */
	if (!reg)
		return;

	reg = readl(p->membase + NPSC_UART_CPR);
	if (!reg)
		return;

	/* Select the type based on fifo */
	if (reg & NPSC_UART_CPR_FIFO_MODE) {
		p->type = PORT_16550A;
		p->flags |= UPF_FIXED_TYPE;
		p->fifosize = NPSC_UART_CPR_FIFO_SIZE(reg);
		up->tx_loadsz = p->fifosize;
		up->capabilities = UART_CAP_FIFO;
	}

	if (reg & NPSC_UART_CPR_AFCE_MODE)
		up->capabilities |= UART_CAP_AFE;
}

static int npsc_probe(struct platform_device *pdev)
{
	struct uart_8250_port uart = {};
	struct resource *regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct resource *irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	struct npsc_data *data;
	int err;

	if (!regs || !irq) {
		dev_err(&pdev->dev, "no registers/irq defined\n");
		return -EINVAL;
	}

	spin_lock_init(&uart.port.lock);
	uart.port.mapbase = regs->start;
	uart.port.irq = irq->start;
	uart.port.handle_irq = npsc_handle_irq;
	uart.port.pm = npsc_do_pm;
	uart.port.type = PORT_8250;
	uart.port.flags = UPF_SHARE_IRQ | UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;
	uart.port.dev = &pdev->dev;

	uart.port.membase = devm_ioremap(&pdev->dev, regs->start,
					 resource_size(regs));
	if (!uart.port.membase)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->clk = devm_clk_get(&pdev->dev, NULL);
	if (!IS_ERR(data->clk)) {
		clk_prepare_enable(data->clk);
		uart.port.uartclk = clk_get_rate(data->clk);
	}

	uart.port.iotype = UPIO_MEM;
	uart.port.serial_in = npsc_serial_in;
	uart.port.serial_out = npsc_serial_out;
	uart.port.private_data = data;

	npsc_setup_port(&uart);

	if (pdev->dev.of_node) {
		err = npsc_probe_of(&uart.port);
		if (err)
			return err;
	} else if (ACPI_HANDLE(&pdev->dev)) {
		err = npsc_probe_acpi(&uart);
		if (err)
			return err;
	} else {
		return -ENODEV;
	}

	data->line = serial8250_register_8250_port(&uart);
	if (data->line < 0)
		return data->line;

	platform_set_drvdata(pdev, data);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int npsc_remove(struct platform_device *pdev)
{
	struct npsc_data *data = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);

	serial8250_unregister_port(data->line);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int npsc_suspend(struct device *dev)
{
	struct npsc_data *data = dev_get_drvdata(dev);

	serial8250_suspend_port(data->line);

	return 0;
}

static int npsc_resume(struct device *dev)
{
	struct npsc_data *data = dev_get_drvdata(dev);

	serial8250_resume_port(data->line);

	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_RUNTIME
static int npsc_runtime_suspend(struct device *dev)
{
	struct npsc_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->clk))
		clk_disable_unprepare(data->clk);

	return 0;
}

static int npsc_runtime_resume(struct device *dev)
{
	struct npsc_data *data = dev_get_drvdata(dev);

	if (!IS_ERR(data->clk))
		clk_prepare_enable(data->clk);

	return 0;
}
#endif

static const struct dev_pm_ops npsc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(npsc_suspend, npsc_resume)
	SET_RUNTIME_PM_OPS(npsc_runtime_suspend, npsc_runtime_resume, NULL)
};

static const struct of_device_id npsc_of_match[] = {
	{ .compatible = "npsc,npsc-apb-uart" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, npsc_of_match);

static const struct acpi_device_id npsc_acpi_match[] = {
	{ "INT33C4", 0 },
	{ "INT33C5", 0 },
	{ "80860F0A", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, npsc_acpi_match);

static struct platform_driver npsc_platform_driver = {
	.driver = {
		.name		= "npsc-apb-uart",
		.owner		= THIS_MODULE,
		.pm		= &npsc_pm_ops,
		.of_match_table	= npsc_of_match,
		.acpi_match_table = ACPI_PTR(npsc_acpi_match),
	},
	.probe			= npsc_probe,
	.remove			= npsc_remove,
};

module_platform_driver(npsc_platform_driver);
