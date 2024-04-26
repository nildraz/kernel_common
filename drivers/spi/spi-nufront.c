#include <linux/clk.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/scatterlist.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/reset.h>

#include "spi-dw.h"

#define DRIVER_NAME "npsc_spi_priv"

struct npsc_spi_priv {
	struct dw_spi  dws;
	struct clk     *clk;
};

extern int npsc_spi_dma_probe(struct platform_device *pdev, struct dw_spi *dws);

static int npsc_spi_mmio_probe(struct platform_device *pdev)
{
	struct npsc_spi_priv *pdata;
	struct dw_spi *dws;
	struct resource *mem, *ioarea;
	struct pinctrl *pinctrl;
	struct device_node *np = pdev->dev.of_node;
	u32 val;
	int ret;

	device_reset(&pdev->dev);

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_warn(&pdev->dev, "pins are not configured from the driver!!\n");
		return -EINVAL;
	}

	pdata = kzalloc(sizeof(struct npsc_spi_priv), GFP_KERNEL);
	if (!pdata) {
		ret = -ENOMEM;
		goto err_end;
	}

	dws = &pdata->dws;

	/* Get basic io resource and map it */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		ret = -EINVAL;
		goto err_kfree;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "SPI region already claimed\n");
		ret = -EBUSY;
		goto err_kfree;
	}
	dws->paddr = mem->start;
	dws->regs = ioremap_nocache(mem->start, resource_size(mem));
	if (!dws->regs) {
		dev_err(&pdev->dev, "SPI region already mapped\n");
		ret = -ENOMEM;
		goto err_release_reg;
	}

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		ret = dws->irq; /* -ENXIO */
		goto err_unmap;
	}

	pdata->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(pdata->clk)) {
		ret = PTR_ERR(pdata->clk);
		goto err_irq;
	}
	clk_prepare_enable(pdata->clk);

	if (!of_property_read_u32(np, "clock-frequency", &val)) {
		clk_set_rate(pdata->clk, val);
		dev_info(&pdev->dev, "actually reference clock %uHz\n",
			(u32)clk_get_rate(pdata->clk));
	}

	if (of_property_read_u32(np, "bus_num", &val)) {
		dev_err(&pdev->dev, "SPI can not get bus_num\n");
		val = 0;
	}
	dws->bus_num = val;

	dws->parent_dev = &pdev->dev;
	dws->num_cs = 4;
	dws->max_freq = clk_get_rate(pdata->clk);

	if(of_property_read_bool(np, "enable-dma"))
		npsc_spi_dma_probe(pdev, dws);

	ret = dw_spi_add_host(dws);
	if (ret)
		goto err_clk;

	platform_set_drvdata(pdev, pdata);
	return 0;

err_clk:
	clk_disable(pdata->clk);
	clk_put(pdata->clk);
	pdata->clk = NULL;
err_irq:
	free_irq(dws->irq, dws);
err_unmap:
	iounmap(dws->regs);
err_release_reg:
	release_mem_region(mem->start, resource_size(mem));
err_kfree:
	kfree(pdata);
err_end:
	return ret;
}

static int npsc_spi_mmio_remove(struct platform_device *pdev)
{
	struct npsc_spi_priv *pdata = platform_get_drvdata(pdev);
	struct resource *mem;

	platform_set_drvdata(pdev, NULL);

	clk_disable_unprepare(pdata->clk);
	clk_put(pdata->clk);
	pdata->clk = NULL;

	free_irq(pdata->dws.irq, &pdata->dws);
	dw_spi_remove_host(&pdata->dws);
	iounmap(pdata->dws.regs);
	kfree(pdata);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	return 0;
}

static int npsc_spi_mmio_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct npsc_spi_priv *pdata = platform_get_drvdata(pdev);
	int ret;

	ret = dw_spi_suspend_host(&pdata->dws);
	if (ret)
		return ret;
	if (pdata->clk)
		clk_disable(pdata->clk);
	return 0;
}

static int npsc_spi_mmio_resume(struct platform_device *pdev)
{
	struct npsc_spi_priv *pdata = platform_get_drvdata(pdev);

	if (pdata->clk)
		clk_enable(pdata->clk);
	return dw_spi_resume_host(&pdata->dws);
}

static const struct of_device_id npsc_spi_of_match[] = {
	{ .compatible = "npsc,mmio-spi", },
	{},
};

static struct platform_driver npsc_spi_mmio_driver = {
	.probe		= npsc_spi_mmio_probe,
	.remove		= npsc_spi_mmio_remove,
	.suspend	= npsc_spi_mmio_suspend,
	.resume		= npsc_spi_mmio_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(npsc_spi_of_match),
	},
};
module_platform_driver(npsc_spi_mmio_driver);
