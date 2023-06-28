// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#define RA_IRQ_REQUEST_REG	0
#define RA_IRQ_MASK_REG		1

struct ra_irq_priv {
	void __iomem *regs;
	struct device *dev;
	struct irq_domain *domain;
	int width;
	spinlock_t lock;
};

static inline u32 ra_irq_ior(struct ra_irq_priv *priv, off_t reg)
{
	switch (priv->width) {
		case 32:
			return ioread32(priv->regs + (reg << 2));
		case 16:
			return (u32) ioread16(priv->regs + (reg << 1));
		default:
			BUG();
	}
}

static inline void ra_irq_iow(struct ra_irq_priv *priv, off_t reg, u32 val)
{
	switch (priv->width) {
		case 32:
			iowrite32(val, priv->regs + (reg << 2));
			break;
		case 16:
			iowrite16((u16) val, priv->regs + (reg << 1));
			break;
		default:
			BUG();
	}
}

static void ra_irq_irqchip_mask(struct irq_data *d)
{
	struct ra_irq_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&priv->lock, flags);
	v = ra_irq_ior(priv, RA_IRQ_MASK_REG);
	v |= BIT(d->hwirq);
	ra_irq_iow(priv, RA_IRQ_MASK_REG, v);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ra_irq_irqchip_unmask(struct irq_data *d)
{
	struct ra_irq_priv *priv = irq_data_get_irq_chip_data(d);
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&priv->lock, flags);
	v = ra_irq_ior(priv, RA_IRQ_MASK_REG);
	v &= ~BIT(d->hwirq);
	ra_irq_iow(priv, RA_IRQ_MASK_REG, v);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static struct irq_chip ra_irq_irq_chip = {
	.irq_mask	= ra_irq_irqchip_mask,
	.irq_mask_ack	= ra_irq_irqchip_mask,
	.irq_unmask	= ra_irq_irqchip_unmask,
};

static int ra_irq_irq_map(struct irq_domain *domain, unsigned int virq,
			  irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &ra_irq_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, domain->host_data);
	// irq_set_noprobe(virq);

	return 0;
}

static const struct irq_domain_ops ra_irq_irq_ops = {
	.map = ra_irq_irq_map,
};

static void ra_irq_irq_handler(struct irq_desc *desc)
{
	struct ra_irq_priv *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	unsigned long pending;
	u32 irqs, mask;
	int hwirq;

	chained_irq_enter(host_chip, desc);

	irqs = ra_irq_ior(priv, RA_IRQ_REQUEST_REG);
	mask = ra_irq_ior(priv, RA_IRQ_MASK_REG);
	pending = irqs & ~mask;

	dev_dbg(priv->dev, "%s(): pending 0x%lx, width %d\n",
		__func__, pending, priv->width);

	for_each_set_bit(hwirq, &pending, priv->width)
		generic_handle_domain_irq(priv->domain, hwirq);

	chained_irq_exit(host_chip, desc);
}

static const struct of_device_id ra_irq_of_ids[] = {
	{ .compatible = "lawo,ravenna-irq-controller-32bit", .data = (void *)32 },
	{ .compatible = "lawo,ravenna-irq-controller-16bit", .data = (void *)16 },
	{}
};
MODULE_DEVICE_TABLE(of, ra_irq_of_ids);

static void ra_irq_domain_remove(void *domain)
{
	irq_domain_remove(domain);
}

static int ra_irq_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct ra_irq_priv *priv;
	struct resource *res;
	int ret, irq;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	spin_lock_init(&priv->lock);

	priv->dev = dev;
	priv->width = (unsigned long)of_device_get_match_data(dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	dev_info(dev, "Ravenna FPGA IRQ controller @%pa, %d-bit\n",
		 &res->start, priv->width);

	irq = of_irq_get(dev->of_node, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get IRQ: %d\n", irq);
		return irq;
	}

	/* initially disable all IRQ sources */
	ra_irq_iow(priv, RA_IRQ_MASK_REG, ~0);

	priv->domain = irq_domain_add_linear(node, priv->width,
					     &ra_irq_irq_ops, priv);
	if (!priv->domain)
		return -ENOMEM;

	ret = devm_add_action_or_reset(dev, ra_irq_domain_remove, priv);
	if (ret < 0)
		return ret;

	irq_set_irq_type(irq, IRQF_TRIGGER_HIGH);
	irq_set_chained_handler_and_data(irq, ra_irq_irq_handler, priv);

	return 0;
}

static struct platform_driver ravenna_irq_driver =
{
	.probe = ra_irq_drv_probe,
	.driver = {
		.name = "ravenna_irq",
		.of_match_table = ra_irq_of_ids,
	},
};

module_platform_driver(ravenna_irq_driver);

MODULE_AUTHOR("Daniel Mack <daniel.mack@holoplot.com>");
MODULE_DESCRIPTION("Ravenna IRQ driver");
MODULE_LICENSE("GPL");
