// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_TX_H
#define RA_SD_TX_H

#include <linux/miscdevice.h>
#include <linux/module.h>

#include "stream-table-tx.h"
#include "track-table.h"

struct ra_sd_tx {
	struct device *dev;
	struct ra_stream_table_tx	sttb;
	struct ra_track_table		trtb;
	struct mutex			mutex;
	struct xarray			streams;
};

struct ra_sd_tx_stream_elem {
	struct ra_sd_tx_stream	stream;
	struct file		*filp;
	struct pid		*pid;
	int			trtb_index;
};

int ra_sd_tx_add_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
			      unsigned int size, void __user *buf);
int ra_sd_tx_update_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
				 unsigned int size, void __user *buf);
int ra_sd_tx_delete_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
				 unsigned int size, void __user *buf);
int ra_sd_tx_delete_streams(struct ra_sd_tx *tx, struct file *filp);
int ra_sd_tx_probe(struct ra_sd_tx *tx, struct device *dev);

#endif /* RA_SD_TX_H */
