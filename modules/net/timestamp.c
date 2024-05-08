// SPDX-License-Identifier: GPL-2.0-or-later

// #define DEBUG 1

#include <linux/irq.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <uapi/linux/net_tstamp.h>

#include "main.h"

void ra_net_tx_ts_irq(struct ra_net_priv *priv)
{
	struct ptp_packet_fpga_timestamp *ts_packet;
	struct device *dev = priv->dev;
	u32 ctr, sot;

	dev_dbg(dev, "%s()\n", __func__);

	spin_lock(&priv->tx_ts.lock);

	if (unlikely((priv->tx_ts.ts_wr_idx+1) % RA_NET_TX_TS_LIST_SIZE ==
		      priv->tx_ts.ts_rd_idx)) {
		priv->tx_ts.reenable_irq = true;
		ra_net_pp_irq_disable(priv, RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE);
		dev_err(dev, "tx timestamp buffer of %s full, IRQ disabled => %08x\n",
			dev_name(dev),  ra_net_ior(priv, RA_NET_PP_IRQ_DISABLE));

		goto out_unlock;
	}

	ts_packet = &priv->tx_ts.fpga_ts[priv->tx_ts.ts_wr_idx];

	// dev_dbg(dev, "TX_TS_COUNT: 0x%08X\n",
	// 	ra_net_ior(priv, RA_NET_PTP_TX_TS_CNT));

	for (ctr = sizeof(*ts_packet) / sizeof(u32); ctr >= 0; ctr--) {
		sot = ra_net_ior(priv, RA_NET_TX_TIMESTAMP_FIFO);

		if ((sot >> 16) == RA_NET_TX_TIMESTAMP_START_OF_TS)
			break;
	}

	if (ctr != sizeof(*ts_packet) / sizeof(u32))
		dev_dbg(dev, "misaligned timestamp for tx packet found\n");

	if (unlikely(ctr == 0)) {
		dev_dbg(dev, "%s(): no start of timestamp found\n", __func__);
		goto out_unlock;
	}

	ts_packet->seconds_hi = sot & 0xffff;

	/* Pull the remaining data */
	ra_net_ior_rep(priv, RA_NET_TX_TIMESTAMP_FIFO,
		       &ts_packet->seconds,
		       sizeof(*ts_packet) - sizeof(ts_packet->seconds_hi));

	dev_dbg(dev, "got timestamp for tx packet, wr_idx %d, seq_id 0x%04x\n",
		priv->tx_ts.ts_wr_idx, ts_packet->sequence_id);

	priv->tx_ts.ts_wr_idx++;
	priv->tx_ts.ts_wr_idx %= RA_NET_TX_TS_LIST_SIZE;

out_unlock:
	spin_unlock(&priv->tx_ts.lock);

	/* schedule always in case of remaining timestamps in list */
	schedule_work(&priv->tx_ts.work);
}

static void ra_net_stamp_tx_skb(struct ra_net_priv *priv, struct sk_buff *skb,
				const struct ptp_packet_fpga_timestamp *ts,
				bool *ts_consumed, bool *skb_consumed)
{
	struct device *dev = priv->dev;
	u8 *data = skb->data;
	u16 packet_seq_id;
	u32 offset;

	offset = ETH_HLEN + IPV4_HLEN(data) + UDP_HLEN;

	*ts_consumed = false;
	*skb_consumed = true;

	/* assumptions:
	 *  - PTP packets are PTPV2 IPV4
	 *  - sequence ID is unique and sufficient to associate timestamp and packet
	 *    (FIXME: is this always true ?)
	 */

	if (skb->len + ETH_HLEN < offset + OFF_PTP_SEQUENCE_ID + sizeof(packet_seq_id)) {
		dev_dbg(dev,  "packet does not contain ptp sequence id (length invalid)\n");
		return;
	}

	packet_seq_id = ntohs(*(__be16*)(data + offset + OFF_PTP_SEQUENCE_ID));

	if (likely(ts->sequence_id == packet_seq_id)) {
		/* OK, timestamp is valid */
		struct skb_shared_hwtstamps shhwtstamps;
		u64 seconds;

		dev_dbg(dev, "found valid timestamp for tx packet; sequence id 0x%04X\n",
			packet_seq_id);

		seconds =  (u64)ts->seconds_hi << 32ULL;
		seconds += (u64)ts->seconds;

		shhwtstamps.hwtstamp =  seconds * NSEC_PER_SEC;
		shhwtstamps.hwtstamp += (u64)ts->nanoseconds;

		*ts_consumed = true;

		skb_tstamp_tx(skb, &shhwtstamps);

		return;
	}

	if (ts->sequence_id < packet_seq_id) {
		/* timestamp without packet ! => remove from list */
		dev_dbg(dev, "timestamp sequence id (0x%04X) < packet sequence id (0x%04X) => discard timestamp\n",
			ts->sequence_id, packet_seq_id);

		*ts_consumed = true;
		*skb_consumed = false;

		return;
	}

	/* if (ts->sequence_id > packet_seq_id) */
	/* corresponding timestamp seems to be lost ! => "remove" packet from list */
	dev_dbg(dev, "timestamp sequence id (0x%04X) > packet sequence id (0x%04X) => discard packet\n",
		ts->sequence_id, packet_seq_id);
}

static void ra_net_tx_ts_work(struct work_struct *work)
{
	struct ra_net_priv *priv =
		container_of(work, struct ra_net_priv, tx_ts.work);
	unsigned long flags;

	dev_dbg(priv->dev, "%s()\n", __func__);

	spin_lock_irqsave(&priv->tx_ts.lock, flags);

	while (priv->tx_ts.skb_wr_idx != priv->tx_ts.skb_rd_idx &&
	       priv->tx_ts.ts_wr_idx  != priv->tx_ts.ts_rd_idx) {
		struct ptp_packet_fpga_timestamp *ts;
		bool ts_consumed, skb_consumed;
		struct sk_buff *skb;

		skb = priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx];
		ts = &priv->tx_ts.fpga_ts[priv->tx_ts.ts_rd_idx];

		ra_net_stamp_tx_skb(priv, skb, ts,
				    &ts_consumed, &skb_consumed);

		if (skb_consumed) {
			dev_kfree_skb_any(skb);
			priv->tx_ts.skb_rd_idx++;
			priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;
		}

		if (ts_consumed) {
			priv->tx_ts.ts_rd_idx++;
			priv->tx_ts.ts_rd_idx %= RA_NET_TX_TS_LIST_SIZE;
		}
	}

	spin_unlock_irqrestore(&priv->tx_ts.lock, flags);

	if (priv->tx_ts.reenable_irq) {
		priv->tx_ts.reenable_irq = false;
		ra_net_pp_irq_enable(priv, RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE);
	}
}

void ra_net_flush_tx_ts(struct ra_net_priv *priv)
{
	unsigned long flags;
	int rd_idx;

	cancel_work_sync(&priv->tx_ts.work);

	spin_lock_irqsave(&priv->tx_ts.lock, flags);

	for (;;) {
		struct ptp_packet_fpga_timestamp ts_packet;
		u32 pp_irqs;

		pp_irqs = ra_net_ior(priv, RA_NET_PP_IRQS);
		if (!(pp_irqs & RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE))
			break;

		ra_net_ior_rep(priv, RA_NET_TX_TIMESTAMP_FIFO,
			       &ts_packet, sizeof(ts_packet));
	}

	/* Tx skb list */
	while (priv->tx_ts.skb_rd_idx != priv->tx_ts.skb_wr_idx) {
		dev_kfree_skb(priv->tx_ts.skb_ptr[rd_idx]);

		priv->tx_ts.skb_rd_idx++;
		priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;
	}

	priv->tx_ts.skb_rd_idx = 0;
	priv->tx_ts.skb_wr_idx = 0;

	priv->tx_ts.ts_rd_idx = 0;
	priv->tx_ts.ts_wr_idx = 0;

	spin_unlock_irqrestore(&priv->tx_ts.lock, flags);
}

bool ra_net_tx_ts_queue(struct ra_net_priv *priv, struct sk_buff *skb)
{
	struct skb_shared_info *skb_sh = skb_shinfo(skb);
	unsigned long flags;

	/* Must be called with priv->lock held! */

	if (!priv->tx_ts.enable || !(skb_sh->tx_flags & SKBTX_HW_TSTAMP))
		return false;

	spin_lock_irqsave(&priv->tx_ts.lock, flags);

	if ((priv->tx_ts.skb_wr_idx+1) % RA_NET_TX_SKB_LIST_SIZE ==
		priv->tx_ts.skb_rd_idx) {
		struct sk_buff *old_skb;

		/* no space left in ringbuffer!
		 * => discard oldest entry
		 */

		old_skb = priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx];
		priv->tx_ts.skb_rd_idx++;
		priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;

		dev_kfree_skb_any(old_skb);

		net_err_ratelimited("%s: skb ringbuffer for timestamping full "
					"=> discarding oldest entry\n",
					priv->ndev->name);
	}

	dev_dbg(priv->dev, "Requesting timestamp for tx packet\n");

	priv->tx_ts.skb_ptr[priv->tx_ts.skb_wr_idx] = skb;
	priv->tx_ts.skb_wr_idx++;
	priv->tx_ts.skb_wr_idx %= RA_NET_TX_SKB_LIST_SIZE;

	spin_unlock_irqrestore(&priv->tx_ts.lock, flags);

	skb_sh->tx_flags |= SKBTX_IN_PROGRESS;

	return true;
}

void ra_net_rx_apply_timestamp(struct ra_net_priv *priv,
			       struct sk_buff *skb,
			       struct ptp_packet_fpga_timestamp *ts)
{
	struct skb_shared_hwtstamps *ts_ptr = skb_hwtstamps(skb);
	u64 ns;

	if (!priv->rx_ts_enable)
		return;

	if (ts->start_of_ts != RA_NET_TX_TIMESTAMP_START_OF_TS) {
		dev_err(priv->dev, "RX timestamp has no SOT\n");
		return;
	}

	dev_dbg(priv->dev, "Valid rx timestamp found\n");

	ns = (s64)ts->seconds * NSEC_PER_SEC + ts->nanoseconds;
	ts_ptr->hwtstamp = ns_to_ktime(ns);
}

void ra_net_rx_read_timestamp(struct ra_net_priv *priv, struct sk_buff *skb)
{
	struct ptp_packet_fpga_timestamp ts;

	BUILD_BUG_ON(!IS_ALIGNED(sizeof(ts), sizeof(u32)));

	ra_net_ior_rep(priv, RA_NET_RX_FIFO, &ts, sizeof(ts));
	ra_net_rx_apply_timestamp(priv, skb, &ts);
}

static void ra_net_tx_ts_config(struct ra_net_priv *priv)
{
	bool on = priv->tx_ts.enable || priv->rx_ts_enable;

	netif_stop_queue(priv->ndev);

	ra_net_iow_mask(priv, RA_NET_PP_CONFIG,
			RA_NET_PP_CONFIG_ENABLE_PTP_TIMESTAMPS,
			on ? RA_NET_PP_CONFIG_ENABLE_PTP_TIMESTAMPS : 0);

	if (on)
		ra_net_pp_irq_enable(priv, RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE);
	else
		ra_net_pp_irq_disable(priv, RA_NET_PP_IRQ_PTP_TX_TS_IRQ_AVAILABLE);

	netif_start_queue(priv->ndev);
}

void ra_net_tx_ts_init(struct ra_net_priv *priv)
{
	spin_lock_init(&priv->tx_ts.lock);
	INIT_WORK(&priv->tx_ts.work, ra_net_tx_ts_work);
}

int ra_net_hwtstamp_ioctl(struct net_device *ndev, struct ifreq *ifr, int cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct hwtstamp_config config;
	struct device *dev = priv->dev;

	dev_dbg(dev, "%s()\n", __func__);

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags) {
		dev_err(dev, "%s(): got config.flags 0x%08X which should be 0.",
		       __func__, config.flags);
		return -EINVAL;
	}

	switch(config.tx_type) {
	case HWTSTAMP_TX_OFF:
		dev_dbg(dev, "%s(): HWTSTAMP_TX_OFF\n", __func__);
		priv->tx_ts.enable = false;
		ra_net_tx_ts_config(priv);
		break;

	case HWTSTAMP_TX_ON:
		dev_dbg(dev, "%s(): HWTSTAMP_TX_ON\n", __func__);
		priv->tx_ts.enable = true;
		ra_net_tx_ts_config(priv);
		break;

	default:
		dev_err(dev, "%s() config.tx_type %d not supported\n",
			__func__, config.tx_type);
		return -EINVAL;
	}

	switch(config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		dev_dbg(dev, "%s(): HWTSTAMP_FILTER_NONE\n", __func__);
		priv->rx_ts_enable = false;
		ra_net_tx_ts_config(priv);
		break;

	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		dev_dbg(dev, "%s(): HWTSTAMP_FILTER_PTP_V2_L4_xxx\n",
			__func__);

		priv->rx_ts_enable = true;
		ra_net_tx_ts_config(priv);

		config.rx_filter = HWTSTAMP_FILTER_PTP_V2_L4_EVENT;
		break;

	default:
		dev_dbg(dev, "%s() config.rx_filter %i not supported\n",
			__func__, config.rx_filter);
		return -EINVAL;
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}
