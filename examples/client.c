// SPDX-License-Identifier: MIT

/*
gcc -o client -Wall -I include/ client.c
*/

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <uapi/ravenna/stream-device.h>

static struct ra_sd_rx_stream rx_stream = {
	.sync_source 		= 0,
	.vlan_tagged 		= 0,
	.hitless_protection 	= 0,
	.synchronous 		= 0,

	.rtp_offset 		= 0,
	.jitter_buffer_margin 	= 0,
	.rtp_ssrc 		= 0,
	.rtp_payload_type 	= 97,

	.codec 			= RA_STREAM_CODEC_L24,
	.num_channels 		= 8,
};

static struct ra_sd_tx_stream tx_stream = {
	.primary = {
		.destination_mac	= { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 },
	},

	.ttl			= 4,
	.dscp_tos		= 0,
	.vlan_tagged		= 0,
	.multicast		= 1,

	.use_primary		= 1,
	.use_secondary		= 0,

	.next_rtp_sequence_num	= 0,

	.rtp_payload_type	= 97,
	.next_rtp_tx_time	= 0,
	.rtp_offset		= 0,
	.rtp_ssrc		= 1234,

	.codec			= RA_STREAM_CODEC_L24,
	.num_samples		= 16,
	.num_channels		= 8,
};

static int add_rx_stream(int fd, int x)
{
	int i;
	struct ra_sd_add_rx_stream_cmd cmd = {
		.version = 0,
		.stream = rx_stream,
	};

	inet_pton(AF_INET, "238.228.114.83", &cmd.stream.primary.destination_ip);
	inet_pton(AF_INET, "192.168.100.1",  &cmd.stream.primary.source_ip);
	cmd.stream.primary.destination_port = htons(5004);

	cmd.stream.primary.destination_ip += x << 24;
	cmd.stream.primary.source_ip += x << 24;

	for (i = 0; i < cmd.stream.num_channels; i++)
		cmd.stream.tracks[i] = i + (x * cmd.stream.num_channels);

	return ioctl(fd, RA_SD_ADD_RX_STREAM, &cmd);
}

static int update_rx_stream(int fd, int x, int index)
{
	int i;
	struct ra_sd_update_rx_stream_cmd cmd = {
		.version = 0,
		.index = index,
		.stream = rx_stream,
	};

	inet_pton(AF_INET, "238.228.114.83", &cmd.stream.primary.destination_ip);
	inet_pton(AF_INET, "192.168.100.1",  &cmd.stream.primary.source_ip);
	cmd.stream.primary.destination_port = htons(5004);

	cmd.stream.primary.destination_ip += x << 24;
	cmd.stream.primary.source_ip += x << 24;

	cmd.stream.num_channels *= 2;

	for (i = 0; i < cmd.stream.num_channels; i++)
		cmd.stream.tracks[i] = i + (x * cmd.stream.num_channels);

	return ioctl(fd, RA_SD_UPDATE_RX_STREAM, &cmd);
}

static int delete_rx_stream(int fd, int index)
{
	struct ra_sd_delete_rx_stream_cmd cmd = {
		.index = index,
	};

	return ioctl(fd, RA_SD_DELETE_RX_STREAM, &cmd);
}

static int add_tx_stream(int fd, int x)
{
	int i;
	struct ra_sd_add_tx_stream_cmd cmd = {
		.version = 0,
		.stream = tx_stream,
	};

	inet_pton(AF_INET, "238.228.114.83", &cmd.stream.primary.destination_ip);
	inet_pton(AF_INET, "192.168.100.1",  &cmd.stream.primary.source_ip);
	cmd.stream.primary.destination_port = htons(5004);
	cmd.stream.primary.source_port = htons(1234);

	cmd.stream.primary.destination_ip += x << 24;
	cmd.stream.primary.source_ip += x << 24;

	for (i = 0; i < cmd.stream.num_channels; i++)
		cmd.stream.tracks[i] = i + (x * cmd.stream.num_channels);

	return ioctl(fd, RA_SD_ADD_TX_STREAM, &cmd);
}

static int update_tx_stream(int fd, int x, int index)
{
	int i;
	struct ra_sd_update_tx_stream_cmd cmd = {
		.version = 0,
		.index = index,
		.stream = tx_stream,
	};

	inet_pton(AF_INET, "238.228.114.83", &cmd.stream.primary.destination_ip);
	inet_pton(AF_INET, "192.168.100.1",  &cmd.stream.primary.source_ip);
	cmd.stream.primary.destination_port = htons(5004);

	cmd.stream.primary.destination_ip += x << 24;
	cmd.stream.primary.source_ip += x << 24;

	cmd.stream.num_channels *= 2;

	for (i = 0; i < cmd.stream.num_channels; i++)
		cmd.stream.tracks[i] = i + (x * cmd.stream.num_channels);

	return ioctl(fd, RA_SD_UPDATE_TX_STREAM, &cmd);
}

static int delete_tx_stream(int fd, int index)
{
	struct ra_sd_delete_tx_stream_cmd cmd = {
		.index = index,
	};

	return ioctl(fd, RA_SD_DELETE_TX_STREAM, &cmd);
}

static int read_rtcp_rx_stat(int fd, int index)
{
	int ret;

	struct ra_sd_read_rtcp_rx_stat_cmd cmd = {
		.index = index,
		.timeout_ms = 1000,
	};

	ret = ioctl(fd, RA_SD_READ_RTCP_RX_STAT, &cmd);
	if (ret < 0) {
		printf("RA_SD_READ_RTCP_RX_STAT failed: %d", -errno);
		return -errno;
	}

	printf("RTCP STATS #%d\n", index);
	printf("  RTP timestamp %d\n", cmd.data.rtp_timestamp);

	return 0;
}

#define NUM_STREAMS 8

int main(int argc, char **argv)
{
	int rx[NUM_STREAMS], tx[NUM_STREAMS];
	int fd, i;

	fd = open("/dev/ravenna-stream-device", O_RDWR);
	assert(fd >= 0);

	read_rtcp_rx_stat(fd, 0);

	for (i = 0; i < NUM_STREAMS; i++) {
		rx[i] = add_rx_stream(fd, i);
		printf("RA_SD_RX_ADD_STREAM returned index %d for stream %d\n", rx[i], i);
		assert(rx[i] >= 0);

		tx[i] = add_tx_stream(fd, i);
		printf("RA_SD_TX_ADD_STREAM returned index %d for stream %d\n", tx[i], i);
		assert(tx[i] >= 0);
	}

	update_rx_stream(fd, rx[1], 16);
	update_rx_stream(fd, rx[2], 17);

	update_tx_stream(fd, tx[1], 16);
	update_tx_stream(fd, tx[2], 17);

	for (i = 0; i < NUM_STREAMS; i++) {
		int ret;

		sleep(10);
		printf("Removing streams with index %d\n", i);

		ret = delete_rx_stream(fd, rx[i]);
		assert(ret == 0);

		ret = delete_tx_stream(fd, tx[i]);
		assert(ret == 0);
	}

	return 0;
}
