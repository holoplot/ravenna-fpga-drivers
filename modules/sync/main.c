// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk.h>
#include <linux/file.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <uapi/ravenna/sync.h>

#include "main.h"

static irqreturn_t ra_sync_irqhandler(int irq, void *devid)
{
	struct ra_sync_priv *priv = devid;

	return IRQ_HANDLED;
}

static int ra_sync_set_frequency_ioctl(struct ra_sync_priv *priv,
				       unsigned int size, void __user *buf)
{
	u32 freq;

	if (size != sizeof(freq))
		return -EINVAL;

	if (copy_from_user(&freq, buf, sizeof(freq)))
		return -EFAULT;

	return clk_set_rate(priv->mclk, freq);
}

static long ra_sync_ioctl(struct file *filp,
			  unsigned int cmd,
			  unsigned long arg)
{
	struct ra_sync_priv *priv = to_ra_sync_priv(filp->private_data);
	void __user *buf = (void __user *)arg;
	unsigned int size = _IOC_SIZE(cmd);

	switch (cmd) {
	case RA_SYNC_SET_MCLK_FREQUENCY:
		return ra_sync_set_frequency_ioctl(priv, size, buf);
	}

	return -ENOTTY;
}

static const struct file_operations ra_sync_fops =
{
	.unlocked_ioctl	= &ra_sync_ioctl,
};

static void ra_sync_mclk_disable_unpreprare(void *c)
{
	clk_disable_unprepare(c);
}

static void ra_sync_misc_deregister(void *misc)
{
	misc_deregister(misc);
}

static const struct regmap_config ra_sync_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.max_register 	= 0x100,
	.reg_stride	= 4,
};

static int ra_sync_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *ptp_node;
	struct ra_sync_priv *priv;
	struct resource *res;
	void __iomem *regs;
	const char *name;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->mutex);
	priv->dev = dev;

	ptp_node = of_parse_phandle(dev->of_node, "lawo,ptp-clock", 0);
	if (ptp_node) {
		struct platform_device *ptp_dev;
		struct ptp_clock *ptp_clock;

		ptp_dev = of_find_device_by_node(ptp_node);
		if (!ptp_dev)
			return -EPROBE_DEFER;

		ptp_clock = platform_get_drvdata(ptp_dev);
		if (!ptp_clock)
			return -EPROBE_DEFER;
	}

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->regmap = devm_regmap_init_mmio(dev, regs, &ra_sync_regmap_config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_irq(dev, irq, ra_sync_irqhandler, IRQF_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "could not request irq: %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(dev->of_node, "lawo,device-name", &name);
	if (ret < 0) {
		dev_err(dev, "No lawo,device-name property: %d\n", ret);
		return ret;
	}

	priv->misc.minor = MISC_DYNAMIC_MINOR;
	priv->misc.fops = &ra_sync_fops;
	priv->misc.name = name;

	ret = misc_register(&priv->misc);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, ra_sync_misc_deregister, &priv->misc);
	if (ret < 0)
		return ret;

	priv->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err(dev, "could not get mclk: %d\n", ret);
		return ret;
	}

	ret = clk_set_rate(priv->mclk, 48000 * 512);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(priv->mclk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, ra_sync_mclk_disable_unpreprare, priv->mclk);
	if (ret < 0)
		return ret;

	ret = ra_sync_debugfs_init(priv);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "Ravenna sync '%s', minor %d",
		 priv->misc.name, priv->misc.minor);

	return 0;
}

static void ra_sync_shutdown(struct platform_device *pdev)
{
	struct ra_sync_priv *priv = platform_get_drvdata(pdev);

	clk_disable_unprepare(priv->mclk);
}

static const struct of_device_id ra_sync_of_ids[] = {
	{ .compatible = "lawo,ravenna-sync" },
	{}
};
MODULE_DEVICE_TABLE(of, ra_sync_of_ids);

static struct platform_driver ra_sync_driver =
{
	.probe = ra_sync_probe,
	.shutdown = ra_sync_shutdown,
	.driver = {
		.name = "ravenna-sync",
		.of_match_table = ra_sync_of_ids,
	},
};
module_platform_driver(ra_sync_driver);

MODULE_AUTHOR("Daniel Mack <daniel.mack@holoplot.com");
MODULE_DESCRIPTION("Ravenna sync driver");
MODULE_LICENSE("GPL");
