// SPDX-License-Identifier: MIT

#ifndef _UAPI_RAVENNA_STREAM_DEVICE_H
#define _UAPI_RAVENNA_STREAM_DEVICE_H

#include <linux/types.h>
#include <linux/ioctl.h>

#include "types.h"

enum {
	RA_SD_STATE_INITIALIZING	= 0,
	RA_SD_STATE_EST_1ST		= 1,
	RA_SD_STATE_LOCK_1ST		= 2,
	RA_SD_STATE_EST_2ND		= 3,
	RA_SD_STATE_LOCK_2ND		= 4,
	RA_SD_STATE_REALIGN		= 5,
};

struct ra_sd_rtcp_rx_data {
	__u32 rtp_timestamp;
	__u8 dev_state;
	__u8 rtp_payload_id;
	__u16 offset_estimation;
	__s32 path_differential;

	struct ra_sd_rtcp_rx_data_interface {
		__u16 misordered_pkts;
		__u16 base_sequence_nr;
		__u32 extended_max_sequence_nr;
		__u32 received_pkts;
		__u16 peak_jitter;
		__u16 estimated_jitter;
		__u16 last_transit_time;
		__u16 current_offset_estimation;
		__u32 last_ssrc;
		__u16 buffer_margin_min;
		__u16 buffer_margin_max;
		__u16 late_pkts;
		__u16 early_pkts;

		__u8 error:1;
		__u8 playing:1;

		__u16 timeout_counter;
	} primary, secondary;
};

struct ra_sd_rtcp_tx_data {
	__u32 rtp_timestamp;			/* DATA_0 */

	struct ra_sd_rtcp_tx_data_interface {
		__u32 sent_pkts;
		__u32 sent_rtp_bytes;
	} primary, secondary;
};

struct ra_sd_read_rtcp_rx_stat_cmd {
	__u32 index;
	__u32 timeout_ms;
	struct ra_sd_rtcp_rx_data data;
};

struct ra_sd_read_rtcp_tx_stat_cmd {
	__u32 index;
	__u32 timeout_ms;
	struct ra_sd_rtcp_tx_data data;
};

/* RX streams */

struct ra_sd_rx_stream {
	struct ra_sd_rx_stream_interface {
		__be32 destination_ip;
		__be32 source_ip;
		__be16 destination_port;
	} primary, secondary;

	__u8 sync_source:1;
	__u8 vlan_tagged:1;
	__u8 hitless_protection:1;
	__u8 synchronous:1;
	__u8 rtp_filter:1;

	__be16 vlan_tag;

	/* RA_SD_RX_STREAM_CODEC_... */
	__u8 codec;
	__u32 rtp_offset;

	__u16 jitter_buffer_margin;
	__u32 rtp_ssrc;

	__u8 rtp_payload_type;

	__u16 num_channels;
	/* Put -1 to route the channel nowhere */
	__s16 tracks[RA_MAX_CHANNELS];
};

struct ra_sd_add_rx_stream_cmd {
	__u32 version;
	struct ra_sd_rx_stream stream;
};

struct ra_sd_update_rx_stream_cmd {
	__u32 version;
	__u32 index;
	struct ra_sd_rx_stream stream;
};

struct ra_sd_delete_rx_stream_cmd {
	__u32 version;
	__u32 index;
};


/* TX streams */

struct ra_sd_tx_stream {
	struct ra_sd_tx_stream_interface {
		__be32 destination_ip;
		__be32 source_ip;
		__be16 destination_port;
		__be16 source_port;
		__be16 vlan_tag;
		__u8 destination_mac[6];
	} primary, secondary;

	__u8 ttl;
	__u8 dscp_tos;

	__u8 vlan_tagged:1;
	__u8 multicast:1;
	__u8 use_primary:1;
	__u8 use_secondary:1;

	/* RA_STREAM_CODEC_... */
	__u8 codec;

	__u16 next_rtp_sequence_num;
	__u8 rtp_payload_type;
	__u8 next_rtp_tx_time;
	__u32 rtp_offset;
	__u32 rtp_ssrc;

	__u8 num_samples;
	__u16 num_channels;
	/* Put -1 to route the channel nowhere */
	__s16 tracks[RA_MAX_CHANNELS];
};

struct ra_sd_add_tx_stream_cmd {
	__u32 version;
	struct ra_sd_tx_stream stream;
};

struct ra_sd_update_tx_stream_cmd {
	__u32 version;
	__u32 index;
	struct ra_sd_tx_stream stream;
};

struct ra_sd_delete_tx_stream_cmd {
	__u32 version;
	__u32 index;
};

#define RA_SD_READ_RTCP_RX_STAT	_IOW('r', 100, struct ra_sd_read_rtcp_rx_stat_cmd)
#define RA_SD_READ_RTCP_TX_STAT	_IOW('r', 101, struct ra_sd_read_rtcp_tx_stat_cmd)

#define RA_SD_ADD_TX_STREAM	_IOW('r', 200, struct ra_sd_add_tx_stream_cmd)
#define RA_SD_UPDATE_TX_STREAM	_IOW('r', 201, struct ra_sd_update_tx_stream_cmd)
#define RA_SD_DELETE_TX_STREAM	_IOW('r', 202, struct ra_sd_delete_tx_stream_cmd)

#define RA_SD_ADD_RX_STREAM	_IOW('r', 300, struct ra_sd_add_rx_stream_cmd)
#define RA_SD_UPDATE_RX_STREAM	_IOW('r', 301, struct ra_sd_update_rx_stream_cmd)
#define RA_SD_DELETE_RX_STREAM	_IOW('r', 302, struct ra_sd_delete_rx_stream_cmd)

#endif /* _UAPI_RAVENNA_STREAM_DEVICE_H */