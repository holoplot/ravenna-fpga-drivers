// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/dmaengine.h>
#include <linux/of_address.h>
#include <linux/etherdevice.h>

#include "main.h"

static void ra_net_dma_release_channel(void *data) {
	struct dma_chan *chan = data;

	dmaengine_terminate_all(chan);
	dma_release_channel(chan);
}

int ra_net_dma_probe(struct ra_net_priv *priv) {
	struct dma_slave_config conf = {};
	struct device_node *node;
	struct resource res;
	int ret;

	node = of_parse_phandle(priv->dev->of_node, "lawo,dma-fifo", 0);
	if (!node) {
		return 0;
	}

	ret = dma_set_mask_and_coherent(priv->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(priv->dev, "DMA mask failed: %d\n", ret);
	}

	ret = of_address_to_resource(node, 0, &res);
	of_node_put(node);
	if (ret < 0) {
		dev_err(priv->dev, "could not get resource for DMA FIFO: %d\n", ret);
		return ret;
	}

	/* RX */
	conf.direction = DMA_DEV_TO_MEM;
	conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	conf.src_addr = res.start;
	conf.src_maxburst = 16;
	conf.dst_maxburst = 16;
	priv->dma_addr = res.start;

	priv->dma_rx_chan = dma_request_chan(priv->dev, "rx");
	if (IS_ERR(priv->dma_rx_chan)) {
		ret = PTR_ERR(priv->dma_rx_chan);
		dev_err(priv->dev, "could not request RX DMA channel: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(priv->dev, ra_net_dma_release_channel,
				       priv->dma_rx_chan);
	if (ret < 0)
		return ret;

	ret = dmaengine_slave_config(priv->dma_rx_chan, &conf);
	if (ret < 0) {
		dev_err(priv->dev, "could not configure RX DMA channel: %d\n", ret);
		return ret;
	}

	return 0;
}

void ra_net_dma_flush(struct ra_net_priv *priv) {
	if (priv->dma_rx_chan)
		dmaengine_terminate_all(priv->dma_rx_chan);
}

/* RX */

struct ra_net_dma_rx_ctx {
	struct ra_net_priv *priv;
	struct sk_buff *skb;
	dma_addr_t dma_addr;
	bool timestamped;
	size_t len, buf_len;
};

static int ra_net_dma_rx_one(struct ra_net_priv *priv);

static void ra_net_dma_rx_callback(void *arg)
{
	struct ra_net_dma_rx_ctx *ctx = arg;
	struct ra_net_priv *priv = ctx->priv;
	struct device *dma_dev;

	dma_dev = dmaengine_get_dma_device(priv->dma_rx_chan);

	dma_unmap_single(dma_dev, ctx->dma_addr,
			 ctx->buf_len, DMA_FROM_DEVICE);

	/* FPGA inserts 2 padding bytes */
	skb_reserve(ctx->skb, RA_NET_RX_PADDING_BYTES);
	skb_put(ctx->skb, ctx->len);

	if (ctx->timestamped) {
		struct ptp_packet_fpga_timestamp *ts =
			(struct ptp_packet_fpga_timestamp *)
				(ctx->skb->data + ctx->len);

		ra_net_rx_apply_timestamp(priv, ctx->skb, ts);
	}

	ctx->skb->protocol = eth_type_trans(ctx->skb, priv->ndev);

	/* FPGA does IP checksum offload for receive packets */
	ctx->skb->ip_summed = CHECKSUM_UNNECESSARY;

	spin_lock(&priv->lock);
	priv->ndev->stats.rx_packets++;
	priv->ndev->stats.rx_bytes += ctx->len;
	spin_unlock(&priv->lock);

	netif_rx(ctx->skb);

	if (netif_queue_stopped(priv->ndev))
		netif_wake_queue(priv->ndev);

	kfree(ctx);

	ra_net_dma_rx(priv);
}

static int ra_net_dma_rx_one(struct ra_net_priv *priv)
{
	u32 status, pkt_len, buf_len;
	struct dma_async_tx_descriptor *tx;
	struct ra_net_dma_rx_ctx *ctx;
	struct device *dma_dev;
	struct sk_buff *skb;
	dma_cookie_t cookie;
	dma_addr_t dma_addr;
	bool timestamped;
	int ret;

	status = ra_net_ior(priv, RA_NET_RX_STATE);
	pkt_len = status & RA_NET_RX_STATE_PACKET_LEN_MASK;
	timestamped = !!(status & RA_NET_RX_STATE_PACKET_HAS_PTP_TS);

	if (pkt_len == 0)
		return -ENOENT;

	buf_len = pkt_len + RA_NET_RX_PADDING_BYTES;

	if (timestamped)
		buf_len += sizeof(struct ptp_packet_fpga_timestamp);

	dma_dev = dmaengine_get_dma_device(priv->dma_rx_chan);

	skb = netdev_alloc_skb(priv->ndev, buf_len+4);
	if (unlikely(!skb)) {
		priv->ndev->stats.rx_fifo_errors++;
		return -ENOMEM;
	}

	dma_addr = dma_map_single(dma_dev, skb->data,
				  buf_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(dma_dev, dma_addr)) {
		dev_err(priv->dev, "Failed to DMA map buffer\n");
		ret = -EIO;
		goto err_free_skb;
	}

	ctx = kzalloc(sizeof(struct ra_net_dma_rx_ctx), GFP_ATOMIC);
	if (!ctx) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	ctx->priv = priv;
	ctx->skb = skb;
	ctx->len = pkt_len;
	ctx->buf_len = buf_len;
	ctx->dma_addr = dma_addr;
	ctx->timestamped = timestamped;

	tx = dmaengine_prep_dma_memcpy(priv->dma_rx_chan, dma_addr,
				       priv->dma_addr, buf_len,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx) {
		dev_err(priv->dev, "dmaengine_prep_dma_memcpy failed\n");
		ret = -EIO;
		goto err_free_ctx;
	}

	tx->callback = ra_net_dma_rx_callback;
	tx->callback_param = ctx;

	cookie = dmaengine_submit(tx);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(priv->dev, "dma_submit_error %d\n", ret);
		goto err_free_ctx;
	}

	dma_async_issue_pending(priv->dma_rx_chan);

	return 0;

err_free_ctx:
	kfree(ctx);
err_unmap:
	dma_unmap_single(dma_dev, dma_addr, buf_len, DMA_FROM_DEVICE);
err_free_skb:
	kfree_skb(skb);

	return ret;
}

void ra_net_dma_rx(struct ra_net_priv *priv) {
	int ret;

	ret = ra_net_dma_rx_one(priv);
	if (ret < 0)
		ra_net_irq_enable(priv, RA_NET_IRQ_RX_PACKET_AVAILABLE);
}
