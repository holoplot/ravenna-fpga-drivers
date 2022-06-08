// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_TRACK_TABLE_H
#define RA_SD_TRACK_TABLE_H

#include <linux/io.h>
#include <linux/seq_file.h>

struct ra_track_table {
	void __iomem	*regs;
	unsigned long	*used_entries;
	int		max_entries;
};

static inline void ra_track_table_write(struct ra_track_table *trtb,
					int index, u32 val)
{
	iowrite32(val, trtb->regs + (index * sizeof(u32)));
}

static inline u32 ra_track_table_read(struct ra_track_table *trtb, int index)
{
	return ioread32(trtb->regs + (index * sizeof(u32)));
}

int ra_track_table_alloc(struct ra_track_table *trtb, int n_channels);
void ra_track_table_set(struct ra_track_table *trtb,
			int index, int n_channels, s16 *tracks);
void ra_track_table_free(struct ra_track_table *trtb,
			 int n_channels, int trtb_index);

size_t ra_track_table_used(const struct ra_track_table *trtb);

int ra_track_table_probe(struct device *dev,
			 struct device_node *np,
			 struct ra_track_table *trtb);

#endif /* RA_SD_STREAM_TABLE_H */
