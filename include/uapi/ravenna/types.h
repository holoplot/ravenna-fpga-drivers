// SPDX-License-Identifier: MIT

#ifndef _UAPI_RAVENNA_TYPES_H
#define _UAPI_RAVENNA_TYPES_H

enum {
	RA_STREAM_CODEC_AM824 = 0,
	RA_STREAM_CODEC_L32 = 1,
	RA_STREAM_CODEC_L24 = 2,
	RA_STREAM_CODEC_L16 = 3,
	_RA_STREAM_CODEC_MAX
};

#define RA_MAX_ETHERNET_PACKET_SIZE	(1460)
#define RA_MAX_CHANNELS			(256)
#define RA_MAX_TRACKS			(256)

/* Used to route a channel to no track */
#define RA_NULL_TRACK			(-1)

#endif /* _UAPI_RAVENNA_TYPES_H */
