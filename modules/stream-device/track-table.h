// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_TRACK_TABLE_H
#define RA_SD_TRACK_TABLE_H

#include <linux/io.h>
#include <linux/seq_file.h>

#define RA_TRACK_TABLE_MUTE 0x100

struct ra_track_table {
	void __iomem	*regs;
	unsigned long	*used_entries;
	int		max_entries;
};

static inline void ra_track_table_write(struct ra_track_table *trtb,
					int index, u32 val)
{
	BUG_ON(index >= trtb->max_entries);
	iowrite32(val, trtb->regs + (index * sizeof(u32)));
}

static inline u32 ra_track_table_read(struct ra_track_table *trtb, int index)
{
	BUG_ON(index >= trtb->max_entries);
	return ioread32(trtb->regs + (index * sizeof(u32)));
}

static inline int ra_stream_find_used_track(int start, int n_channels,
					    const s16 *tracks)
{
	int i;

	for (i = start; i < n_channels; i++)
		if (tracks[i] >= 0)
			break;

	return i;
}

#define FOR_EACH_TRACK(i,n_channels,tracks)				\
	for (i = ra_stream_find_used_track(0, (n_channels), (tracks));	\
	     i < (n_channels);						\
	     i = ra_stream_find_used_track((i) + 1, (n_channels), (tracks)))

int ra_track_table_alloc(struct ra_track_table *trtb, int n_channels);
void ra_track_table_set(struct ra_track_table *trtb,
			int index, int n_channels, const s16 *tracks);
void ra_track_table_free(struct ra_track_table *trtb,
			 int n_channels, int trtb_index);
int ra_track_table_probe(struct device *dev,
			 struct device_node *np,
			 struct ra_track_table *trtb);

#endif /* RA_SD_STREAM_TABLE_H */
