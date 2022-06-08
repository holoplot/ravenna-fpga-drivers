// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_CODEC_H
#define RA_SD_CODEC_H

#include <uapi/ravenna/types.h>

static inline const char *ra_sd_codec_str(int codec)
{
	switch (codec) {
	case RA_STREAM_CODEC_AM824:
		return "AM824";
	case RA_STREAM_CODEC_L32:
		return "32-bit";
	case RA_STREAM_CODEC_L24:
		return "24-bit";
	case RA_STREAM_CODEC_L16:
		return "16-bit";
	default:
		return "UNKNOWN";
	}
}

static inline u8 ra_sd_codec_fpga_code(int codec)
{
	switch (codec) {
	case RA_STREAM_CODEC_AM824:
		return 0xa8;
	case RA_STREAM_CODEC_L32:
		return 0x20;
	case RA_STREAM_CODEC_L24:
		return 0x18;
	case RA_STREAM_CODEC_L16:
		return 0x10;
	default:
		BUG();
	}
}

static inline int ra_sd_codec_sample_length(int codec)
{
	switch (codec) {
	case RA_STREAM_CODEC_AM824:
		return 8;
	case RA_STREAM_CODEC_L32:
		return 4;
	case RA_STREAM_CODEC_L24:
		return 3;
	case RA_STREAM_CODEC_L16:
		return 2;
	default:
		BUG();
	}
}

#endif /* RA_SD_CODEC_H */
