// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_RTCP_H
#define RA_SD_RTCP_H

#include <linux/module.h>

struct ra_sd_rtcp_rx_data_fpga {
#ifdef __LITTLE_ENDIAN
	u32 rtp_timestamp;			/* DATA_0 */
	u16 pri_base_sequence_nr;		/* DATA_1 */
	u16 pri_misordered_pkts;
	u32 pri_extended_max_sequence_nr;	/* DATA_2 */
	u32 pri_received_pkts;			/* DATA_3 */
	u16 pri_estimated_jitter;		/* DATA_4 */
	u16 pri_peak_jitter;
	u16 sec_base_sequence_nr;		/* DATA_5 */
	u16 sec_misordered_pkts;
	u32 sec_extended_max_sequence_nr;	/* DATA_6 */
	u32 sec_received_pkts;			/* DATA_7 */
	u16 sec_estimated_jitter;		/* DATA_8 */
	u16 sec_peak_jitter;
	u16 pri_current_offset_estimation;	/* DATA_9 */
	u16 pri_last_transit_time;
	u32 pri_last_ssrc;			/* DATA_10 */
	u16 pri_buffer_margin_max;		/* DATA_11 */
	u16 pri_buffer_margin_min;
	u16 pri_early_pkts;			/* DATA_12 */
	u16 pri_late_pkts;
	u16 sec_current_offset_estimation;	/* DATA_13 */
	u16 sec_last_transit_time;
	u32 sec_last_ssrc;			/* DATA_14 */
	u16 sec_buffer_margin_max;		/* DATA_15 */
	u16 sec_buffer_margin_min;
	u16 sec_early_pkts;			/* DATA_16 */
	u16 sec_late_pkts;
	u32 flags_1;				/* DATA_17 */
	u32 flags_2;				/* DATA_18 */
#else
#error Big Endian platforms are unsupported
#endif
} __packed;

struct ra_sd_rtcp_tx_data_fpga {
	u32 rtp_timestamp;			/* DATA_0 */
	u32 pri_sent_pkts;			/* DATA_1 */
	u32 pri_sent_rtp_bytes;			/* DATA_2 */
	u32 sec_sent_pkts;			/* DATA_3 */
	u32 sec_sent_rtp_bytes;			/* DATA_4 */
} __packed;

struct ra_sd_priv;

void ra_sd_rtcp_rx_irq(struct ra_sd_priv *priv);
void ra_sd_rtcp_tx_irq(struct ra_sd_priv *priv);

int ra_sd_read_rtcp_rx_stat_ioctl(struct ra_sd_priv *priv,
				  unsigned int size,
				  void __user *buf);
int ra_sd_read_rtcp_tx_stat_ioctl(struct ra_sd_priv *priv,
				  unsigned int size,
				  void __user *buf);

#endif /* RA_SD_RTCP_H */
