// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
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

static int ra_sync_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ra_sync_priv *priv;
	const char *name;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->mutex);
	priv->dev = dev;

	irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_irq(dev, irq, ra_sync_irqhandler, IRQF_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "could not request irq: %d\n", ret);
		return ret;
	}

	priv->mclk = devm_clk_get(dev, "mclk");
	if (IS_ERR(priv->mclk)) {
		ret = PTR_ERR(priv->mclk);
		dev_err(dev, "could not get mclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(priv->mclk);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, ra_sync_mclk_disable_unpreprare, priv->mclk);
	if (ret < 0)
		return ret;

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

	ret = ra_sync_debugfs_init(priv);
	if (ret < 0)
		return ret;

	dev_info(&pdev->dev, "Ravenna sync '%s', minor %d",
		 priv->misc.name, priv->misc.minor);

	return 0;
}

static const struct of_device_id ra_sync_of_ids[] = {
	{ .compatible = "lawo,ravenna-sync" },
	{}
};
MODULE_DEVICE_TABLE(of, ra_sync_of_ids);

static struct platform_driver ra_sync_driver =
{
	.probe = ra_sync_probe,
	.driver = {
		.name = "ravenna-sync",
		.of_match_table = ra_sync_of_ids,
	},
};
module_platform_driver(ra_sync_driver);

MODULE_AUTHOR("Daniel Mack <daniel.mack@holoplot.com");
MODULE_DESCRIPTION("Ravenna RX driver");
MODULE_LICENSE("GPL");
