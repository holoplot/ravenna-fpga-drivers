// SPDX-License-Identifier: GPL-2.0-or-later

//#define DEBUG 1

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>

#include "main.h"

static int ra_net_napi_poll(struct napi_struct *napi, int budget)
{
	struct ra_net_priv *priv = container_of(napi, struct ra_net_priv, napi);
	int count;

	for (count = 0; count < budget; count++) {
		struct sk_buff *skb;

		u32 status = ra_net_ior(priv, RA_NET_RX_STATE);
		u32 pkt_len = status & RA_NET_RX_STATE_PACKET_LEN_MASK;
		u32 pkt_len_padded = ALIGN(pkt_len + RA_NET_RX_PADDING_BYTES,
					   sizeof(u32));

		if (pkt_len == 0)
			break;

		dev_dbg(priv->dev, "%s() pkt_len %d\n", __func__, pkt_len);

		skb = napi_alloc_skb(&priv->napi, pkt_len_padded+4);
		if (unlikely(!skb)) {
			priv->ndev->stats.rx_fifo_errors++;
			break;
		}

		ra_net_ior_rep(priv, RA_NET_RX_FIFO, skb->data,	pkt_len_padded);

		/* FPGA inserts 2 padding bytes */
		skb_reserve(skb, RA_NET_RX_PADDING_BYTES);
		skb_put(skb, pkt_len);

		skb->protocol = eth_type_trans(skb, priv->ndev);

		/* FPGA does IP checksum offload for receive packets */
		skb->ip_summed = CHECKSUM_UNNECESSARY;

		if (status & RA_NET_RX_STATE_PACKET_HAS_PTP_TS)
			ra_net_rx_read_timestamp(priv, skb);

		priv->ndev->stats.rx_packets++;
		priv->ndev->stats.rx_bytes += pkt_len;

		// skb_dump(KERN_DEBUG, skb, true);

		napi_gro_receive(&priv->napi, skb);
	}

	if (count < budget)
		napi_complete_done(&priv->napi, count);

	if (netif_queue_stopped(priv->ndev))
		netif_wake_queue(priv->ndev);

	ra_net_irq_enable(priv, RA_NET_IRQ_RX_PACKET_AVAILABLE);

	return count;
}

static irqreturn_t ra_net_irqhandler(int irq, void *dev_id)
{
	struct ra_net_priv *priv = dev_id;
	struct net_device *ndev = priv->ndev;
	struct device *dev = priv->dev;
	u32 mask, pp_mask, irqs, pp_irqs;

	dev_dbg(dev, "%s()\n", __func__);

	spin_lock(&priv->reg_lock);
	mask = ra_net_ior(priv, RA_NET_IRQ_DISABLE);
	irqs = ra_net_ior(priv, RA_NET_IRQS) & ~mask;

	pp_mask = ra_net_ior(priv, RA_NET_PP_IRQ_DISABLE);
	pp_irqs = ra_net_ior(priv, RA_NET_PP_IRQS) & ~pp_mask;
	spin_unlock(&priv->reg_lock);

	if (!irqs && !pp_irqs)
		return IRQ_NONE;

	dev_dbg(dev, "irqs 0x%04x pp_irqs 0x%04x\n", irqs, pp_irqs);

	if (irqs & RA_NET_IRQ_RX_OVERRUN) {
		ndev->stats.rx_over_errors =
			ra_net_ior(priv, RA_NET_RX_PACKET_DROPPED_CNT) -
			priv->rx_dropped_packets_at_probe;
	}

	if (irqs & RA_NET_IRQ_RX_PACKET_AVAILABLE) {
		ra_net_irq_disable(priv, RA_NET_IRQ_RX_PACKET_AVAILABLE);

		if (priv->dma_rx_chan)
			ra_net_dma_rx(priv);
		else
			napi_schedule(&priv->napi);
	}

	if (irqs & RA_NET_IRQ_TX_SPACE_AVAILABLE) {
		ra_net_irq_disable(priv, RA_NET_IRQ_TX_SPACE_AVAILABLE);

		priv->tx_throttle = false;

		netif_wake_queue(ndev);
	}

	if (irqs & RA_NET_IRQ_TX_EMPTY) {
		/* Nothing to do */
		ra_net_irq_disable(priv, RA_NET_IRQ_TX_EMPTY);
	}

	if (pp_irqs & RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE)
		ra_net_tx_ts_irq(priv);

	return IRQ_HANDLED;
}

static int ra_net_init(struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;
	u32 mac_features;

	mac_features = ra_net_ior(priv, RA_NET_MAC_FEATURES);
	if (mac_features & RA_NET_MAC_FEATURE_VLAN)
		ndev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;
	else
		dev_info(dev, "device does not support VLAN filtering\n");

	return 0;
}

static void ra_net_flush_rx_fifo(struct ra_net_priv *priv)
{
	for (;;) {
		u32 status = ra_net_ior(priv, RA_NET_RX_STATE);
		u32 pkt_len = status & RA_NET_RX_STATE_PACKET_LEN_MASK;

		pkt_len = DIV_ROUND_UP(pkt_len, sizeof(u32));

		if (pkt_len == 0)
			break;

		while (pkt_len--)
			ra_net_ior(priv, RA_NET_RX_FIFO);
	}
}

static void ra_net_reset(struct ra_net_priv *priv)
{
	ra_net_irq_disable(priv, ~0);
	ra_net_pp_irq_disable(priv, ~0);

	ra_net_flush_rx_fifo(priv);
	ra_net_flush_tx_ts(priv);

	ra_net_dma_flush(priv);

	priv->tx_throttle = false;
}

static void ra_net_write_mac_addr(struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	u32 val;

	val = ndev->dev_addr[0] << 8 |
	      ndev->dev_addr[1];
	ra_net_iow(priv, RA_NET_MAC_ADDR_H, val);

	val = ndev->dev_addr[2] << 24 |
	      ndev->dev_addr[3] << 16 |
	      ndev->dev_addr[4] << 8 |
	      ndev->dev_addr[5] << 0;
	ra_net_iow(priv, RA_NET_MAC_ADDR_L, val);
}

static int ra_net_open(struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;
	int ret;

	dev_dbg(dev, "%s()\n", __func__);

	ret = phylink_of_phy_connect(priv->phylink, dev->of_node, 0);
	if (ret) {
		dev_err(dev, "phylink_of_phy_connect() failed: %d\n", ret);
		return ret;
	}

	phylink_start(priv->phylink);
	ra_net_reset(priv);

	napi_enable(&priv->napi);

	ra_net_irq_enable(priv, RA_NET_IRQ_RX_PACKET_AVAILABLE |
				RA_NET_IRQ_RX_OVERRUN);

	netif_start_queue(ndev);

	return 0;
}

static int ra_net_stop(struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;

	dev_dbg(dev, "%s()\n", __func__);

	phylink_stop(priv->phylink);
	phylink_disconnect_phy(priv->phylink);

	netif_stop_queue(ndev);
	napi_disable(&priv->napi);
	ra_net_reset(priv);

	return 0;
}

static int ra_net_hw_xmit_skb(struct sk_buff *skb, struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	bool free_skb = true, short_packet = false;
	struct device *dev = priv->dev;
	unsigned int len = skb->len;
	unsigned int aligned_len;
	int ret = 0;
	u32 free;
	u8 *buf, *tmp_buf = NULL;

	dev_dbg(dev, "%s()\n", __func__);

	if (unlikely(skb->len <= 0)) {
		dev_dbg(dev, "invalid packet len (skb->len): %d\n", skb->len);
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}

	if (skb->len < ETH_ZLEN) {
		len = ETH_ZLEN;
		short_packet = true;
	}

	/* Adjust length and round to 32bit for FPGA access */
	aligned_len = ALIGN(len + RA_NET_TX_PADDING_BYTES, sizeof(u32));

	/*
	 * We have to copy data to a local buffer if one of the two conditions
	 * aren't met for the provided sk_buff:
	 * - it does not provide enough headroom to insert the padding bytes we
	 *   need for FPGA internal reasons (2 Bytes for length insertion)
	 * - since the packet "on the wire" must be at least ETH_ZLEN (60)
	 *   Bytes long, there must be enough space after the data to ensure
	 *   that we don't transmit data from outside (after) the buffer.
	 *   The needed tailroom space is 60Bytes - data_length.
	 */
	if ((skb_headroom(skb) < RA_NET_TX_PADDING_BYTES) ||
	    (short_packet && (skb_tailroom(skb) < (ETH_ZLEN - skb->len)))) {
		net_dbg_ratelimited("%s: skb->data needs copy, because skb_headroom (%i < %i) "
				    "or skb_tailroom (%i < %i) is too small\n",
				    ndev->name,
				    skb_headroom(skb), RA_NET_TX_PADDING_BYTES,
				    skb_tailroom(skb), (ETH_ZLEN - skb->len));

		tmp_buf = kzalloc(aligned_len, GFP_ATOMIC);
		if (!tmp_buf) {
			ret = -ENOMEM;
			goto out;
		}

		/* FPGA wants 2 bytes padding before data to insert packet length */
		memcpy(tmp_buf + RA_NET_TX_PADDING_BYTES, skb->data, skb->len);
		buf = tmp_buf;
	} else {
		/* shift begin of data 2 bytes left, the FPGA inserts packet length */
		buf = (u8 *)skb->data - RA_NET_TX_PADDING_BYTES;
	}

	// dev_dbg(dev, "TX PKT LENGTH 0x%04x (%d); BUF 0x%p\n", len, len, buf);

	spin_lock(&priv->lock);

	free = ra_net_ior(priv, RA_NET_TX_STATE) & RA_NET_TX_STATE_SPACE_AVAILABLE_MASK;

	if (free < RA_NET_TX_FIFO_MIN_SPACE_AVAILABLE) {
		dev_dbg(dev, "TX FIFO space is running low: %d\n", free);

		priv->tx_throttle = true;
		netif_stop_queue(ndev);

		/* Make sure the queue is stopped before the IRQ is enabled. */
		wmb();

		ra_net_iow(priv, RA_NET_TX_FIFO_SPACE_AV_BYTECNT,
			   RA_NET_TX_FIFO_MIN_SPACE_AVAILABLE);
		ra_net_irq_enable(priv, RA_NET_IRQ_TX_SPACE_AVAILABLE);
	}

	if (free < aligned_len) {
		ret = -ENOSPC;
		free_skb = false;
		goto out_unlock;
	}

	/*
	 * Update statistics now, since the legacy module in FPGA does not
	 * handle the data
	 * => what is written to the FIFO will not create "visible" errors
	 * afterwards
	 */
	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += len;

	dev_dbg(dev, "Transmitting packet: len = %d; aligned = %d\n",
		len, aligned_len);

	ra_net_iow_rep(priv, RA_NET_TX_FIFO, buf, aligned_len);

	if (ra_net_tx_ts_queue(priv, skb)) {
		/* tell FPGA to timestamp this packet */
		len |= RA_NET_TX_CONFIG_TIMESTAMP_PACKET;
		free_skb = false;
	}

	/* start transmission of data */
	ra_net_iow(priv, RA_NET_TX_CONFIG, len);

	/* dummy access needed by FPGA to have enough clock cycles */
	ra_net_ior(priv, RA_NET_TX_STATE);

	// skb_dump(KERN_DEBUG, skb, true);

out_unlock:
	spin_unlock(&priv->lock);

out:
	kfree(tmp_buf);

	if (free_skb)
		dev_kfree_skb_any(skb);

	if (!priv->tx_throttle)
		netif_wake_queue(ndev);

	return ret;
}

static netdev_tx_t ra_net_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	int ret;

	dev_dbg(priv->dev, "%s()\n", __func__);

	ret = ra_net_hw_xmit_skb(skb, ndev);
	if (unlikely(ret == -ENOSPC)) {
		net_dbg_ratelimited("%s: No space in TX FIFO.", ndev->name);

		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

static void ra_net_tx_timeout(struct net_device *ndev, unsigned int txqueue)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	dev_warn(priv->dev, "timeout! txqueue = %d\n", txqueue);

	ra_net_reset(priv);
	netif_trans_update(ndev);
	netif_wake_queue(ndev);
}

static void ra_net_set_rx_mode(struct net_device *ndev)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	unsigned long flags;
	u32 ctrl;

	dev_dbg(priv->dev, "%s\n", __func__);

	/*
	 * Short explanation of multicast handling:
	 *
	 * Normally this function is called by in ioctl
	 * (IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP) if a process wants to
	 * subscribe or unsubscribe a multicast group for this interface.
	 * The MAC doesn't implement a possibility to filter different
	 * multicast adresses. There's just one bit to activate or deactivate
	 * the reception of packets with multicast addresses.
	 */

	spin_lock_irqsave(&priv->reg_lock, flags);

	ctrl = ra_net_ior(priv, RA_NET_MAC_RX_CTRL);
	ctrl &= ~RA_NET_MAC_RX_CTRL_PROMISCUOUS_EN;
	ctrl &= ~RA_NET_MAC_RX_CTRL_MULTICAST_EN;

	if (ndev->flags & IFF_PROMISC) {
		dev_dbg(priv->dev, "IFF_PROMISC\n");
		ctrl |= RA_NET_MAC_RX_CTRL_PROMISCUOUS_EN;
	}

	if (ndev->flags & IFF_ALLMULTI) {
		dev_dbg(priv->dev, "IFF_ALLMULTI\n");
		ctrl |= RA_NET_MAC_RX_CTRL_MULTICAST_EN;
	}

	if (!netdev_mc_empty(ndev)) {
		ctrl |= RA_NET_MAC_RX_CTRL_MULTICAST_EN;

		if (netdev_mc_count(ndev) > 1)
			dev_dbg(priv->dev, "IP_ADD_MEMBERSHIP / IP_DROP_MEMBERSHIP is"
					   " not supported in this network device.\n");
	}

	ra_net_iow(priv, RA_NET_MAC_RX_CTRL, ctrl);

	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static int ra_net_eth_ioctl(struct net_device *ndev, struct ifreq *rq, int cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	if (!netif_running(ndev))
		return -EINVAL;

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return ra_net_hwtstamp_ioctl(ndev, rq, cmd);

	case SIOCGHWTSTAMP:
		return ra_net_hwtstamp_get(ndev, rq);

	default:
		return phylink_mii_ioctl(priv->phylink, rq, cmd);
	}
}

static int ra_net_vlan_rx_add_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	dev_dbg(priv->dev, "%s() vid=%d\n", __func__, vid);

	ra_net_iow_mask(priv, RA_NET_VLAN_CTRL_ARRAY + (vid / 32) * sizeof(u32),
			BIT(vid % 32), BIT(vid % 32));

	ra_net_iow_mask(priv, RA_NET_VLAN_CTRL,
			RA_NET_VLAN_CTRL_VLAN_EN,
			RA_NET_VLAN_CTRL_VLAN_EN);

	return 0;
}

static int ra_net_vlan_rx_kill_vid(struct net_device *ndev, __be16 proto, u16 vid)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	int i;

	dev_dbg(priv->dev, "%s() vid = %d\n", __func__, vid);

	ra_net_iow_mask(priv, RA_NET_VLAN_CTRL_ARRAY + (vid / 32) * sizeof(u32),
			BIT(vid % 32), 0);

	/* Clear VLAN enable bit if bitmap is empty */

	for (i = 0; i < 4096/32; i++)
		if (ra_net_ior(priv, RA_NET_VLAN_CTRL_ARRAY + i * sizeof(u32)))
			return 0;

	ra_net_iow_mask(priv, RA_NET_VLAN_CTRL, RA_NET_VLAN_CTRL_VLAN_EN, 0);

	return 0;
}

static int ra_net_set_mac_address(struct net_device *ndev, void *addr)
{
	struct sockaddr *sa = addr;

	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	dev_addr_set(ndev, sa->sa_data);
	ra_net_write_mac_addr(ndev);

	return 0;
}

static const struct net_device_ops ra_net_netdev_ops =
{
	.ndo_init		= ra_net_init,
	.ndo_open		= ra_net_open,
	.ndo_stop		= ra_net_stop,
	.ndo_start_xmit		= ra_net_start_xmit,
	.ndo_tx_timeout		= ra_net_tx_timeout,
	.ndo_set_rx_mode	= ra_net_set_rx_mode,
	.ndo_eth_ioctl		= ra_net_eth_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_vlan_rx_add_vid	= ra_net_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid	= ra_net_vlan_rx_kill_vid,
	.ndo_set_mac_address	= ra_net_set_mac_address,
};

/* platform device */

static int ra_net_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *ptp_node, *node = dev->of_node;
	struct net_device *ndev;
	struct ra_net_priv *priv;
	struct resource *res;
	u32 val, tmp;
	int irq, ret;

	ndev = devm_alloc_etherdev(dev, sizeof(*priv));
	if (!ndev)
		return -ENOMEM;

	priv = netdev_priv(ndev);
	priv->dev = dev;
	priv->ndev = ndev;

	platform_set_drvdata(pdev, ndev);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->reg_lock);
	spin_lock_init(&priv->mdio_lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	val = ra_net_ior(priv, RA_NET_ID);
	if (val != RA_NET_ID_VALUE && val != RA_NET_ID_VALUE_2) {
		dev_err(dev, "Invalid content in ID register: 0x%08x\n", val);
		return -ENODEV;
	}

	ra_net_irq_disable(priv, ~0);
	ra_net_pp_irq_disable(priv, ~0);

	irq = of_irq_get_byname(node, "pp");
	ret = devm_request_irq(dev, irq, ra_net_irqhandler, IRQF_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "could not request irq: %d\n", ret);
		return ret;
	}

	priv->phc_index = -1;

	ptp_node = of_parse_phandle(node, "lawo,ptp-clock", 0);
	if (ptp_node) {
		struct ptp_clock *ptp_clock;
		struct platform_device *ptp_dev;

		ptp_dev = of_find_device_by_node(ptp_node);
		if (!ptp_dev)
			return -EPROBE_DEFER;

		ptp_clock = platform_get_drvdata(ptp_dev);
		if (!ptp_clock)
			return -EPROBE_DEFER;

		priv->phc_index = ptp_clock_index(ptp_clock);
	}

	if (priv->phc_index < 0)
		dev_err(dev, "Unable to obtain PTP clock");

	ra_net_tx_ts_init(priv);

	ndev->irq = irq;
	ndev->netdev_ops = &ra_net_netdev_ops;
	ndev->min_mtu = 68;
	ndev->max_mtu = RA_NET_MAX_MTU;
	ndev->sysfs_groups[0] = &ra_net_attr_group;
	ndev->ethtool_ops = &ra_net_ethtool_ops;

	strcpy(ndev->name, "ra%d");
	SET_NETDEV_DEV(ndev, dev);
	netif_napi_add(ndev, &priv->napi, ra_net_napi_poll);

	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	ra_net_write_mac_addr(ndev);

	ether_setup(ndev);

	ret = ra_net_mdio_init(priv);
	if (ret < 0) {
		dev_err(dev, "mdio init failed: %d\n", ret);
		return ret;
	}

	ret = ra_net_phylink_init(priv);
	if (ret < 0) {
		dev_err(dev, "phylink init failed: %d\n", ret);
		return ret;
	}

	ret = ra_net_dma_probe(priv);
	if (ret < 0) {
		dev_err(dev, "DMA init failed: %d\n", ret);
		return ret;
	}

	tmp = 0;
	of_property_read_u32(node, "lawo,ptp-delay-path-rx-1000mbit-nsec", &tmp);
	val = tmp & 0xffff;

	tmp = 0;
	of_property_read_u32(node, "lawo,ptp-delay-path-rx-100mbit-nsec", &tmp);
	val |= tmp << 16;

	if (val != 0) {
		dev_dbg(dev, "RA_NET_PTP_DELAY_ADJUST_1 = 0x%08x\n", val);
		ra_net_iow(priv, RA_NET_PTP_DELAY_ADJUST_1, val);
	}

	tmp = 0;
	of_property_read_u32(node, "lawo,ptp-delay-path-rx-10mbit-nsec", &tmp);
	val = tmp & 0xffff;

	tmp = 0;
	of_property_read_u32(node, "lawo,ptp-delay-path-tx-nsec", &tmp);
	val |= tmp << 16;

	if (val != 0) {
		dev_dbg(dev, "RA_NET_PTP_DELAY_ADJUST_2 = 0x%08x\n", val);
		ra_net_iow(priv, RA_NET_PTP_DELAY_ADJUST_2, val);
	}

	tmp = 5000;
	of_property_read_u32(node, "lawo,watchdog-timeout-ms", &tmp);
	ndev->watchdog_timeo = msecs_to_jiffies(tmp);

	priv->rx_dropped_packets_at_probe = ra_net_ior(priv, RA_NET_RX_PACKET_DROPPED_CNT);

	ret = devm_register_netdev(dev, ndev);
	if (ret < 0) {
		dev_err(dev, "could not register network device: %d\n", ret);
		return ret;
	}

	val = ra_net_ior(priv, RA_NET_RAV_CORE_VERSION);

	dev_info(dev, "Ravenna ethernet driver, core version: %02x.%02x, %s mode\n",
		 (val >> 8) & 0xff, val & 0xff,
		 priv->dma_rx_chan ? "DMA" : "FIFO");

	return 0;
}

static const struct of_device_id imx_rav_net_of_ids[] = {
	{ .compatible = "lawo,ravenna-ethernet" },
	{}
};
MODULE_DEVICE_TABLE(of, imx_rav_net_of_ids);

static struct platform_driver ravenna_net_driver =
{
	.probe = ra_net_drv_probe,
	.driver = {
		.name = "ravenna_net_driver",
		.of_match_table = imx_rav_net_of_ids,
	},
};

module_platform_driver(ravenna_net_driver);

MODULE_DESCRIPTION("Ravenna ethernet driver");
MODULE_LICENSE("GPL");
