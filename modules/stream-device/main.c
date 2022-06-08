// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "main.h"

static irqreturn_t ra_sd_irqhandler(int irq, void *devid)
{
	struct ra_sd_priv *priv = devid;
	irqreturn_t ret = IRQ_NONE;
	u32 irqs;

	irqs = ra_sd_ior(priv, RA_SD_IRQ_REQUEST);

	if (irqs & RA_SD_IRQ_RTCP_RX) {
		ra_sd_rtcp_rx_irq(priv);
		ret = IRQ_HANDLED;
	}

	if (irqs & RA_SD_IRQ_RTCP_TX) {
		ra_sd_rtcp_tx_irq(priv);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static long ra_sd_ioctl(struct file *filp,
			unsigned int cmd,
			unsigned long arg)
{
	struct ra_sd_priv *priv = to_ra_sd_priv(filp->private_data);
	void __user *buf = (void __user *)arg;
	unsigned int size = _IOC_SIZE(cmd);

	switch (cmd) {
	case RA_SD_READ_RTCP_RX_STAT:
		return ra_sd_read_rtcp_rx_stat_ioctl(priv, size, buf);

	case RA_SD_READ_RTCP_TX_STAT:
		return ra_sd_read_rtcp_tx_stat_ioctl(priv, size, buf);

	case RA_SD_ADD_TX_STREAM:
		return ra_sd_tx_add_stream_ioctl(&priv->tx, filp, size, buf);

	case RA_SD_UPDATE_TX_STREAM:
		return ra_sd_tx_update_stream_ioctl(&priv->tx, filp, size, buf);

	case RA_SD_DELETE_TX_STREAM:
		return ra_sd_tx_delete_stream_ioctl(&priv->tx, filp, size, buf);

	case RA_SD_ADD_RX_STREAM:
		return ra_sd_rx_add_stream_ioctl(&priv->rx, filp, size, buf);

	case RA_SD_UPDATE_RX_STREAM:
		return ra_sd_rx_update_stream_ioctl(&priv->rx, filp, size, buf);

	case RA_SD_DELETE_RX_STREAM:
		return ra_sd_rx_delete_stream_ioctl(&priv->rx, filp, size, buf);
	}

	return -ENOTTY;
}

static int ra_sd_release(struct inode *inode, struct file *filp)
{
	struct ra_sd_priv *priv = to_ra_sd_priv(filp->private_data);

	ra_sd_rx_delete_streams(&priv->rx, filp);
	ra_sd_tx_delete_streams(&priv->tx, filp);

	return 0;
}

static const struct file_operations ra_sd_fops =
{
	.unlocked_ioctl	= &ra_sd_ioctl,
	.release	= &ra_sd_release,
};

static void ra_sd_misc_deregister(void *misc)
{
	misc_deregister(misc);
}

static int ra_sd_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ra_sd_priv *priv;
	struct resource *res;
	const char *name;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->rtcp_rx.mutex);
	mutex_init(&priv->rtcp_tx.mutex);

	init_waitqueue_head(&priv->rtcp_rx.wait);
	init_waitqueue_head(&priv->rtcp_tx.wait);

	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	irq = of_irq_get(dev->of_node, 0);
	ret = devm_request_irq(dev, irq, ra_sd_irqhandler, IRQF_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "could not request irq: %d\n", ret);
		return ret;
	}

	ret = ra_sd_rx_probe(&priv->rx, dev);
	if (ret < 0) {
		dev_err(dev, "RX setup failed: %d\n", ret);
		return ret;
	}

	ret = ra_sd_tx_probe(&priv->tx, dev);
	if (ret < 0) {
		dev_err(dev, "TX setup failed: %d\n", ret);
		return ret;
	}

	ret = of_property_read_string(dev->of_node, "lawo,device-name", &name);
	if (ret < 0) {
		dev_err(dev, "No lawo,device-name property: %d\n", ret);
		return ret;
	}

	priv->misc.minor = MISC_DYNAMIC_MINOR;
	priv->misc.fops = &ra_sd_fops;
	priv->misc.name = name;

	ret = misc_register(&priv->misc);
	if (ret < 0)
		return ret;

	ret = devm_add_action_or_reset(dev, ra_sd_misc_deregister, &priv->misc);
	if (ret < 0)
		return ret;

	ret = ra_sd_debugfs_init(priv);
	if (ret < 0)
		return ret;

	/* Reset hash table */
	ra_sd_iow(priv, RA_SD_RX_HSTB_CLEAR, 0);

	ra_sd_iow(priv, RA_SD_CONFIG,
		  RA_SD_CONFIG_RTCP_RX | RA_SD_CONFIG_RTCP_TX);

	ra_sd_iow(priv, RA_SD_IRQ_MASK,
		  RA_SD_IRQ_RTCP_RX | RA_SD_IRQ_RTCP_TX);

	dev_info(dev, "Ravenna stream-device '%s', minor %d",
		 priv->misc.name, priv->misc.minor);

	return 0;
}

static const struct of_device_id ra_sd_of_ids[] = {
	{ .compatible = "lawo,ravenna-stream-device" },
	{}
};
MODULE_DEVICE_TABLE(of, ra_sd_of_ids);

static struct platform_driver ra_sd_driver =
{
	.probe = ra_sd_probe,
	.driver = {
		.name = "ravenna-stream-device",
		.of_match_table = ra_sd_of_ids,
	},
};
module_platform_driver(ra_sd_driver);

MODULE_AUTHOR("Daniel Mack <daniel.mack@holoplot.com");
MODULE_DESCRIPTION("Ravenna RX driver");
MODULE_LICENSE("GPL");
