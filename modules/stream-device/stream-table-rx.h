// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_STREAM_TABLE_RX_H
#define RA_SD_STREAM_TABLE_RX_H

#include <linux/seq_file.h>
#include <uapi/ravenna/stream-device.h>

struct ra_stream_table_rx {
	void __iomem	*regs;
	int		max_entries;
};

void ra_stream_table_rx_set(struct ra_stream_table_rx *sttb,
			    struct ra_sd_rx_stream *stream,
			    int index, int trtb_index,
			    bool invalidate);

void ra_stream_table_rx_del(struct ra_stream_table_rx *sttb,
			    int index);

void ra_stream_table_rx_dump(struct ra_stream_table_rx *sttb,
			     struct seq_file *s);

int ra_stream_table_rx_probe(struct device *dev,
			     struct device_node *np,
			     struct ra_stream_table_rx *sttb);

#endif /* RA_SD_STREAM_TABLE_H */
