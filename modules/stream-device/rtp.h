// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_RTP_H
#define RA_SD_RTP_H

static inline int ra_sd_validate_rtp_payload_type(int rtp_payload_type,
						  int num_channels,
						  int codec)
{
	/* RFC 3550 */

	switch (rtp_payload_type) {
	case 10:
		if (num_channels != 2 || codec != RA_STREAM_CODEC_L16)
			return -EINVAL;

		break;

	case 11:
		if (num_channels != 1 || codec != RA_STREAM_CODEC_L16)
			return -EINVAL;

		break;

	case 95 ... 127:
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

#endif /* RA_SD_RTP_H */
