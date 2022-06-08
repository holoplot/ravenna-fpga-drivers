// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_RX_H
#define RA_SD_RX_H

#include <linux/miscdevice.h>
#include <linux/module.h>

#include "stream-table-rx.h"
#include "track-table.h"

struct ra_sd_rx {
	struct device *dev;
	struct ra_stream_table_rx	sttb;
	struct ra_track_table		trtb;
	struct mutex			mutex;
	struct xarray			streams;
	unsigned long			*used_tracks;
};

struct ra_sd_rx_stream_elem {
	struct ra_sd_rx_stream	stream;
	struct file		*filp;
	struct pid		*pid;
	int			trtb_index;
};

int ra_sd_rx_add_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
			      unsigned int size, void __user *buf);
int ra_sd_rx_update_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
				 unsigned int size, void __user *buf);
int ra_sd_rx_delete_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
				 unsigned int size, void __user *buf);
int ra_sd_rx_delete_streams(struct ra_sd_rx *rx, struct file *filp);
int ra_sd_rx_probe(struct ra_sd_rx *rx, struct device *dev);

#endif /* RA_SD_RX_H */
