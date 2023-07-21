// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/wait.h>

#include "main.h"
#include "rtcp.h"

void ra_sd_rtcp_rx_irq(struct ra_sd_priv *priv)
{
	ra_sd_read_rtcp_rx(priv, &priv->rtcp_rx.data);
	WRITE_ONCE(priv->rtcp_rx.ready, true);
	wake_up(&priv->rtcp_rx.wait);
}

void ra_sd_rtcp_tx_irq(struct ra_sd_priv *priv)
{
	ra_sd_read_rtcp_tx(priv, &priv->rtcp_tx.data);
	WRITE_ONCE(priv->rtcp_tx.ready, true);
	wake_up(&priv->rtcp_tx.wait);
}

static void ra_sd_parse_rtcp_rx_data(struct ra_sd_rtcp_rx_data_fpga *from,
				     struct ra_sd_rtcp_rx_data *to)
{
	to->rtp_timestamp = from->rtp_timestamp;
	to->dev_state = from->flags_1 & 0x7;
	to->rtp_payload_id = from->flags_1 >> 25;
	to->offset_estimation = (from->flags_1 >> 7) & 0x1ffff;
	to->path_differential = (from->flags_2 >> 8) & 0x7ffff;

	to->primary.misordered_pkts = from->pri_misordered_pkts;
	to->primary.base_sequence_nr = from->pri_base_sequence_nr;
	to->primary.extended_max_sequence_nr = from->pri_extended_max_sequence_nr;
	to->primary.received_pkts = from->pri_received_pkts;
	to->primary.peak_jitter = from->pri_peak_jitter;
	to->primary.estimated_jitter = from->pri_estimated_jitter;
	to->primary.last_transit_time = from->pri_last_transit_time;
	to->primary.current_offset_estimation = from->pri_current_offset_estimation;
	to->primary.last_ssrc = from->pri_last_ssrc;
	to->primary.buffer_margin_min = from->pri_buffer_margin_min;
	to->primary.buffer_margin_max = from->pri_buffer_margin_max;
	to->primary.late_pkts = from->pri_late_pkts;
	to->primary.early_pkts = from->pri_early_pkts;
	to->primary.error = from->flags_1 & BIT(5);
	to->primary.playing = from->flags_1 & BIT(3); // XXX DATASHEET BUG
	to->primary.timeout_counter = from->flags_2 & 0xf;

	to->secondary.misordered_pkts = from->sec_misordered_pkts;
	to->secondary.base_sequence_nr = from->sec_base_sequence_nr;
	to->secondary.extended_max_sequence_nr = from->sec_extended_max_sequence_nr;
	to->secondary.received_pkts = from->sec_received_pkts;
	to->secondary.peak_jitter = from->sec_peak_jitter;
	to->secondary.estimated_jitter = from->sec_estimated_jitter;
	to->secondary.last_transit_time = from->sec_last_transit_time;
	to->secondary.current_offset_estimation = from->sec_current_offset_estimation;
	to->secondary.last_ssrc = from->sec_last_ssrc;
	to->secondary.buffer_margin_min = from->sec_buffer_margin_min;
	to->secondary.buffer_margin_max = from->sec_buffer_margin_max;
	to->secondary.late_pkts = from->sec_late_pkts;
	to->secondary.early_pkts = from->sec_early_pkts;
	to->secondary.error = from->flags_1 & BIT(6);
	to->secondary.playing = from->flags_1 & BIT(4); // XXX DATASHEET BUG
	to->secondary.timeout_counter = (from->flags_2 >> 4) & 0xf;
}

static void ra_sd_parse_rtcp_tx_data(struct ra_sd_rtcp_tx_data_fpga *from,
				     struct ra_sd_rtcp_tx_data *to)
{
	to->rtp_timestamp = from->rtp_timestamp;
	to->primary.sent_pkts = from->pri_sent_pkts;
	to->primary.sent_rtp_bytes = from->pri_sent_rtp_bytes;
	to->secondary.sent_pkts = from->sec_sent_pkts;
	to->secondary.sent_rtp_bytes = from->sec_sent_rtp_bytes;
}

int ra_sd_read_rtcp_rx_stat_ioctl(struct ra_sd_priv *priv,
				  unsigned int size,
				  void __user *buf)
{
	struct ra_sd_read_rtcp_rx_stat_cmd cmd;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.index > 127)
		return -EINVAL;

	mutex_lock(&priv->rtcp_rx.mutex);

	WRITE_ONCE(priv->rtcp_rx.ready, false);
	ra_sd_iow(priv, RA_SD_RX_PAGE_SELECT, cmd.index);

	ret = wait_event_interruptible_timeout(priv->rtcp_rx.wait,
					       priv->rtcp_rx.ready,
					       msecs_to_jiffies(cmd.timeout_ms));
	if (ret == 0)
		ret = -ETIMEDOUT;

	if (ret < 0)
		goto out_unlock;

	/* Report elapsed time back to userspace */
	cmd.timeout_ms -= jiffies_to_msecs(ret);
	ret = 0;

	ra_sd_parse_rtcp_rx_data(&priv->rtcp_rx.data, &cmd.data);

	if (copy_to_user(buf, &cmd, sizeof(cmd)))
		ret = -EFAULT;

out_unlock:
	mutex_unlock(&priv->rtcp_rx.mutex);

	return ret;
}

int ra_sd_read_rtcp_tx_stat_ioctl(struct ra_sd_priv *priv,
				  unsigned int size,
				  void __user *buf)
{
	struct ra_sd_read_rtcp_tx_stat_cmd cmd;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.index > 127)
		return -EINVAL;

	mutex_lock(&priv->rtcp_tx.mutex);

	WRITE_ONCE(priv->rtcp_tx.ready, false);
	ra_sd_iow(priv, RA_SD_TX_PAGE_SELECT, cmd.index);

	ret = wait_event_interruptible_timeout(priv->rtcp_tx.wait,
					       priv->rtcp_tx.ready,
					       msecs_to_jiffies(cmd.timeout_ms));
	if (ret == 0)
		ret = -ETIMEDOUT;

	if (ret < 0)
		goto out_unlock;

	/* Report elapsed time back to userspace */
	cmd.timeout_ms -= jiffies_to_msecs(ret);
	ret = 0;

	ra_sd_parse_rtcp_tx_data(&priv->rtcp_tx.data, &cmd.data);

	if (copy_to_user(buf, &cmd, sizeof(cmd)))
		ret = -EFAULT;

out_unlock:
	mutex_unlock(&priv->rtcp_tx.mutex);

	return ret;
}
