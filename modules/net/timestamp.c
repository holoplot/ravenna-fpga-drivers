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
	int ctr;
	u32 sot;

	dev_dbg(dev, "%s()\n", __func__);

	spin_lock(&priv->tx_ts.lock);

	if (unlikely((priv->tx_ts.ts_wr_idx+1) % RA_NET_TX_TS_LIST_SIZE ==
		      priv->tx_ts.ts_rd_idx)) {
		net_err_ratelimited("%s: tx timestamp buffer full, dropping oldest entry\n",
				    dev_name(dev));
		priv->tx_ts.ts_rd_idx++;
		priv->tx_ts.ts_rd_idx %= RA_NET_TX_TS_LIST_SIZE;
	}

	ts_packet = &priv->tx_ts.fpga_ts[priv->tx_ts.ts_wr_idx];

	// dev_dbg(dev, "TX_TS_COUNT: 0x%08X\n",
	// 	ra_net_ior(priv, RA_NET_PTP_TX_TS_CNT));

	for (ctr = sizeof(*ts_packet) / sizeof(u32) - 1; ctr >= 0; ctr--) {
		sot = ra_net_ior(priv, RA_NET_TX_TIMESTAMP_FIFO);

		if ((sot >> 16) == RA_NET_TX_TIMESTAMP_START_OF_TS)
			break;
	}

	if (ctr != sizeof(*ts_packet) / sizeof(u32) - 1)
		dev_dbg(dev, "misaligned timestamp for tx packet found\n");

	if (unlikely(ctr < 0)) {
		dev_dbg(dev, "%s(): no start of timestamp found\n", __func__);
		priv->tx_ts.ts_lost++;
		goto out_unlock;
	}

	ts_packet->seconds_hi = sot & 0xffff;

	/* Pull the remaining data (one u32 already consumed as sot above) */
	ra_net_ior_rep(priv, RA_NET_TX_TIMESTAMP_FIFO,
		       &ts_packet->seconds,
		       sizeof(*ts_packet) - offsetof(typeof(*ts_packet), seconds));

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
				struct skb_shared_hwtstamps *shhwtstamps,
				bool *ts_consumed, bool *skb_consumed,
				bool *do_stamp)
{
	struct device *dev = priv->dev;
	u8 *data = skb->data;
	u16 packet_seq_id;
	u32 offset;

	offset = ETH_HLEN + IPV4_HLEN(data) + UDP_HLEN;

	*ts_consumed = false;
	*skb_consumed = true;
	*do_stamp = false;

	/* assumptions:
	 *  - PTP packets are PTPV2 IPV4
	 *  - sequence ID is unique and sufficient to associate timestamp and packet
	 *    (FIXME: is this always true ?)
	 */

	if (skb->len < offset + OFF_PTP_SEQUENCE_ID + sizeof(packet_seq_id)) {
		dev_dbg(dev,  "packet does not contain ptp sequence id (length invalid)\n");
		*ts_consumed = true;
		return;
	}

	packet_seq_id = ntohs(*(__be16*)(data + offset + OFF_PTP_SEQUENCE_ID));

	if (likely(ts->sequence_id == packet_seq_id)) {
		/* OK, timestamp is valid */
		u64 seconds;

		dev_dbg(dev, "found valid timestamp for tx packet; sequence id 0x%04X\n",
			packet_seq_id);

		seconds =  (u64)ts->seconds_hi << 32ULL;
		seconds += (u64)ts->seconds;

		shhwtstamps->hwtstamp =  seconds * NSEC_PER_SEC;
		shhwtstamps->hwtstamp += (u64)ts->nanoseconds;

		*ts_consumed = true;
		*do_stamp = true;

		return;
	}

	if (ts->sequence_id < packet_seq_id) {
		/* timestamp without packet ! => remove from list */
		net_err_ratelimited("%s: timestamp sequence id (0x%04X) < packet sequence id (0x%04X), discarding timestamp\n",
			priv->ndev->name, ts->sequence_id, packet_seq_id);

		*ts_consumed = true;
		*skb_consumed = false;

		return;
	}

	net_err_ratelimited("%s: timestamp sequence id (0x%04X) > packet sequence id (0x%04X), discarding packet\n",
		priv->ndev->name, ts->sequence_id, packet_seq_id);
}

static void ra_net_tx_ts_work(struct work_struct *work)
{
	struct ra_net_priv *priv =
		container_of(work, struct ra_net_priv, tx_ts.work);
	unsigned long flags;

	dev_dbg(priv->dev, "%s()\n", __func__);

	spin_lock_irqsave(&priv->tx_ts.lock, flags);

	/* Drain any skbs whose timestamps were lost in the IRQ handler.
	 * The FPGA read words from its FIFO without storing a ts entry,
	 * so the skb list is ahead by ts_lost entries. Discard those skbs
	 * now so the two lists stay synchronised for the correlation loop.
	 */
	while (priv->tx_ts.ts_lost > 0 &&
	       priv->tx_ts.skb_wr_idx != priv->tx_ts.skb_rd_idx) {
		struct sk_buff *skb = priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx];

		net_err_ratelimited("%s: lost FPGA timestamp, discarding skb without stamp\n",
				    priv->ndev->name);

		dev_kfree_skb_any(skb);
		priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx] = NULL;
		priv->tx_ts.skb_rd_idx++;
		priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;
		priv->tx_ts.ts_lost--;
	}

	while (priv->tx_ts.skb_wr_idx != priv->tx_ts.skb_rd_idx &&
	       priv->tx_ts.ts_wr_idx  != priv->tx_ts.ts_rd_idx) {
		struct skb_shared_hwtstamps shhwtstamps = {};
		struct ptp_packet_fpga_timestamp *ts;
		bool ts_consumed, skb_consumed, do_stamp;
		struct sk_buff *skb;

		skb = priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx];
		ts = &priv->tx_ts.fpga_ts[priv->tx_ts.ts_rd_idx];

		ra_net_stamp_tx_skb(priv, skb, ts, &shhwtstamps,
				    &ts_consumed, &skb_consumed, &do_stamp);

		if (WARN_ON(!skb_consumed && !ts_consumed))
			break;

		if (skb_consumed) {
			priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx] = NULL;

			priv->tx_ts.skb_rd_idx++;
			priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;
		}

		if (ts_consumed) {
			priv->tx_ts.ts_rd_idx++;
			priv->tx_ts.ts_rd_idx %= RA_NET_TX_TS_LIST_SIZE;
		}

		if (do_stamp) {
			/* Deliver the timestamp outside the spinlock: skb_tstamp_tx
			 * acquires the socket error queue lock, which must not be
			 * nested inside a driver spinlock held with IRQs disabled.
			 */
			spin_unlock_irqrestore(&priv->tx_ts.lock, flags);
			skb_tstamp_tx(skb, &shhwtstamps);
			dev_kfree_skb_any(skb);
			spin_lock_irqsave(&priv->tx_ts.lock, flags);
		} else if (skb_consumed) {
			dev_kfree_skb_any(skb);
		}
	}

	spin_unlock_irqrestore(&priv->tx_ts.lock, flags);
}

void ra_net_flush_tx_ts(struct ra_net_priv *priv)
{
	unsigned long flags;
	int i;

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
	for (i = 0; i < RA_NET_TX_SKB_LIST_SIZE; i++) {
		dev_kfree_skb_any(priv->tx_ts.skb_ptr[i]);
		priv->tx_ts.skb_ptr[i] = NULL;
	}

	priv->tx_ts.skb_rd_idx = 0;
	priv->tx_ts.skb_wr_idx = 0;

	priv->tx_ts.ts_rd_idx = 0;
	priv->tx_ts.ts_wr_idx = 0;
	priv->tx_ts.ts_lost = 0;

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
		dev_kfree_skb_any(priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx]);
		priv->tx_ts.skb_ptr[priv->tx_ts.skb_rd_idx] = NULL;

		net_err_ratelimited("%s: skb ringbuffer for timestamping full\n",
				    priv->ndev->name);

		priv->tx_ts.skb_rd_idx++;
		priv->tx_ts.skb_rd_idx %= RA_NET_TX_SKB_LIST_SIZE;
	}

	dev_dbg(priv->dev, "Requesting timestamp for tx packet\n");

	priv->tx_ts.skb_ptr[priv->tx_ts.skb_wr_idx] = skb;
	priv->tx_ts.skb_wr_idx++;
	priv->tx_ts.skb_wr_idx %= RA_NET_TX_SKB_LIST_SIZE;

	skb_sh->tx_flags |= SKBTX_IN_PROGRESS;

	spin_unlock_irqrestore(&priv->tx_ts.lock, flags);

	return true;
}

void ra_net_rx_apply_timestamp(struct ra_net_priv *priv,
			       struct sk_buff *skb,
			       struct ptp_packet_fpga_timestamp *ts)
{
	struct skb_shared_hwtstamps *ts_ptr = skb_hwtstamps(skb);
	u64 seconds;
	s64 ns;

	if (!priv->rx_ts_enable)
		return;

	if (ts->start_of_ts != RA_NET_TX_TIMESTAMP_START_OF_TS) {
		dev_err(priv->dev, "RX timestamp has no SOT\n");
		return;
	}

	dev_dbg(priv->dev, "Valid rx timestamp found\n");

	seconds = ((u64)ts->seconds_hi << 32) | ts->seconds;
	ns = (s64)seconds * NSEC_PER_SEC + ts->nanoseconds;
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

int ra_net_hwtstamp_get(struct net_device *ndev, struct ifreq *ifr)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct hwtstamp_config config = {};

	config.tx_type = priv->tx_ts.enable ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	config.rx_filter = priv->rx_ts_enable ?
		HWTSTAMP_FILTER_PTP_V2_L4_EVENT : HWTSTAMP_FILTER_NONE;

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
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

	/* Validate both fields before applying either, to avoid partial config
	 * on error (e.g. TX armed but RX filter unsupported → -EINVAL).
	 */
	switch(config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		dev_err(dev, "%s() config.tx_type %d not supported\n",
			__func__, config.tx_type);
		return -EINVAL;
	}

	switch(config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
		break;
	default:
		dev_dbg(dev, "%s() config.rx_filter %i not supported\n",
			__func__, config.rx_filter);
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
	}

	if (copy_to_user(ifr->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	return 0;
}
