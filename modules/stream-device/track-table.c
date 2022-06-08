// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/of_platform.h>
#include <linux/of_address.h>

#include "track-table.h"

#define RA_TRACK_TABLE_MUTE 0x100

int ra_track_table_alloc(struct ra_track_table *trtb, int n_channels)
{
	unsigned long start;

	/* Allocate a contingous area in the track table */
	start = bitmap_find_next_zero_area(trtb->used_entries,
					   trtb->max_entries,
					   0, n_channels, 0);
	if (start > trtb->max_entries)
		return -ENOSPC;

	return start;
}

void ra_track_table_set(struct ra_track_table *trtb,
			int index, int n_channels, s16 *tracks)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		u32 v = tracks[i] > 0 ? tracks[i] : RA_TRACK_TABLE_MUTE;

		set_bit(index+i, trtb->used_entries);
		ra_track_table_write(trtb, index+i, v);
	}
}

void ra_track_table_free(struct ra_track_table *trtb,
			 int index, int n_channels)
{
	int i;

	for (i = 0; i < n_channels; i++) {
		clear_bit(index+i, trtb->used_entries);
		ra_track_table_write(trtb, index+i, RA_TRACK_TABLE_MUTE);
	}
}

void ra_track_table_reset(struct ra_track_table *trtb)
{
	int i;

	for (i = 0; i < trtb->max_entries; i++)
		ra_track_table_write(trtb, i, RA_TRACK_TABLE_MUTE);

	bitmap_clear(trtb->used_entries, 0, trtb->max_entries);
}

size_t ra_track_table_used(const struct ra_track_table *trtb)
{
	return bitmap_weight(trtb->used_entries, trtb->max_entries);
}

int ra_track_table_probe(struct device *dev,
			 struct device_node *np,
			 struct ra_track_table *trtb)
{
	resource_size_t size;
	struct resource res;
	int ret;

	ret = of_address_to_resource(np, 0, &res);
	if (ret < 0) {
		dev_err(dev, "Failed to access track table resource: %d", ret);
		return ret;
	}

	size = resource_size(&res);

	if (!IS_ALIGNED(size, sizeof(u32))) {
		dev_err(dev, "Invalid resource size for track table");
		return -EINVAL;
	}

	trtb->regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(trtb->regs)) {
		dev_err(dev, "Failed to map resource for track table");
		return PTR_ERR(trtb->regs);
	}

	trtb->max_entries = size / sizeof(u32);

	trtb->used_entries = devm_bitmap_zalloc(dev, trtb->max_entries, GFP_KERNEL);
	if (!trtb->used_entries)
		return -ENOMEM;

	ra_track_table_reset(trtb);

	dev_info(dev, "Ravenna track table, %d entries", trtb->max_entries);

	return 0;
}