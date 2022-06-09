// SPDX-License-Identifier: GPL-2.0-or-later

#define DEBUG 1

#include <linux/of.h>

#include "main.h"
#include "rtp.h"

static struct ra_sd_rx_stream_elem *
ra_sd_rx_stream_elem_find_by_index(struct ra_sd_rx *rx, int index)
{
	struct ra_sd_rx_stream_elem *e = xa_load(&rx->streams, index);

	return xa_is_err(e) ? NULL : e;
}

static int
ra_sd_rx_validate_stream_interface(const struct ra_sd_rx_stream_interface *iface)
{
	if (iface->destination_ip != 0 &&
	    iface->destination_port == 0)
		return -EINVAL;

	return 0;
}

static int ra_sd_rx_validate_stream(const struct ra_sd_rx_stream *stream)
{
	int i, ret;

	if (stream->primary.destination_ip == 0 &&
	    stream->secondary.destination_ip == 0)
		return -EINVAL;

	ret = ra_sd_rx_validate_stream_interface(&stream->primary);
	if (ret < 0)
		return ret;

	ret = ra_sd_rx_validate_stream_interface(&stream->secondary);
	if (ret < 0)
		return ret;

	if (stream->vlan_tagged && be16_to_cpu(stream->vlan_tag) > 4095)
		return -EINVAL;

	if (stream->codec >= _RA_STREAM_CODEC_MAX)
		return -EINVAL;

	if (stream->num_channels > RA_MAX_CHANNELS)
		return -EINVAL;

	ret = ra_sd_validate_rtp_payload_type(stream->rtp_payload_type,
					      stream->num_channels,
					      stream->codec);
	if (ret < 0)
		return ret;

	for (i = 0; i < stream->num_channels; i++)
		if (stream->tracks[i] >= RA_MAX_TRACKS)
			return -EINVAL;

	return 0;
}

static int ra_sd_rx_tracks_available(const struct ra_sd_rx *rx,
				     const struct ra_sd_rx_stream *stream)
{
	int i;

	for (i = 0; i < stream->num_channels; i++)
		if (stream->tracks[i] > 0)
			if (test_bit(stream->tracks[i], rx->used_tracks))
				return -EBUSY;

	return 0;
}

static void ra_sd_rx_tracks_alloc(struct ra_sd_rx *rx,
				  const struct ra_sd_rx_stream *stream)
{
	int i;

	for (i = 0; i < stream->num_channels; i++)
		if (stream->tracks[i] > 0)
			set_bit(stream->tracks[i], rx->used_tracks);
}

static void ra_sd_rx_tracks_free(struct ra_sd_rx *rx,
				 const struct ra_sd_rx_stream *stream)
{
	int i;

	for (i = 0; i < stream->num_channels; i++)
		if (stream->tracks[i] > 0)
			clear_bit(stream->tracks[i], rx->used_tracks);
}

int ra_sd_rx_add_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
			      unsigned int size, void __user *buf)
{
	struct ra_sd_add_rx_stream_cmd cmd;
	struct ra_sd_rx_stream_elem *e;
	u32 index;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	ret = ra_sd_rx_validate_stream(&cmd.stream);
	if (ret < 0)
		return ret;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->filp = filp;
	e->pid = get_pid(task_pid(current));
	memcpy(&e->stream, &cmd.stream, sizeof(e->stream));

	mutex_lock(&rx->mutex);

	ret = ra_sd_rx_tracks_available(rx, &e->stream);
	if (ret < 0)
		goto out_unlock;

	ret = xa_alloc(&rx->streams, &index, e,
		       XA_LIMIT(0, rx->sttb.max_entries), GFP_KERNEL);
	if (ret < 0) {
		dev_err(rx->dev, "xa_alloc() failed: %d\n", ret);
		goto out_unlock;
	}

	ret = ra_track_table_alloc(&rx->trtb, e->stream.num_channels);
	if (ret < 0) {
		dev_err(rx->dev, "ra_track_table_alloc() failed: %d\n", ret);
		xa_erase(&rx->streams, index);
		goto out_unlock;
	}

	e->trtb_index = ret;

	ra_sd_rx_tracks_alloc(rx, &e->stream);
	ra_track_table_set(&rx->trtb, e->trtb_index,
			   e->stream.num_channels, e->stream.tracks);
	ra_stream_table_rx_set(&rx->sttb, &e->stream, index,
			       e->trtb_index, true);

	dev_dbg(rx->dev, "Added RX stream with index %d", index);

out_unlock:
	mutex_unlock(&rx->mutex);

	if (ret < 0) {
		kfree(e);
		return ret;
	}

	return index;
}

int ra_sd_rx_update_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
				 unsigned int size, void __user *buf)
{
	struct ra_sd_update_rx_stream_cmd cmd;
	struct ra_sd_rx_stream_elem *e;
	bool invalidate = false;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	ret = ra_sd_rx_validate_stream(&cmd.stream);
	if (ret < 0)
		return ret;

	mutex_lock(&rx->mutex);

	e = ra_sd_rx_stream_elem_find_by_index(rx, cmd.index);
	if (!e) {
		ret = -ENOENT;
		goto out_unlock;
	}

	/* Streams can only be updated by their creators */
	if (e->filp != filp) {
		ret = -EACCES;
		goto out_unlock;
	}

	if (e->stream.num_channels != cmd.stream.num_channels) {
		/*
		* If the number of channels changes, we need to free the current
		* track table allocation and reserve a new range of tracks.
		*/
		ra_sd_rx_tracks_free(rx, &e->stream);
		ret = ra_sd_rx_tracks_available(rx, &cmd.stream);
		if (ret < 0) {
			/* Roll back */
			ra_sd_rx_tracks_alloc(rx, &e->stream);
			goto out_unlock;
		}

		ra_track_table_free(&rx->trtb, e->trtb_index, e->stream.num_channels);
		ret = ra_track_table_alloc(&rx->trtb, cmd.stream.num_channels);
		if (ret < 0) {
			int aret = ret;

			dev_err(rx->dev, "ra_track_table_alloc() failed: %d\n", ret);

			/* Roll back */
			ra_sd_rx_tracks_alloc(rx, &e->stream);

			ret = ra_track_table_alloc(&rx->trtb,
						   e->stream.num_channels);
			/*
			 * Can't really happen because the allocation was
			 * valid before.
			 */
			if (WARN_ON(ret < 0))
				goto out_unlock;

			e->trtb_index = ret;
			ra_track_table_set(&rx->trtb, e->trtb_index,
					   e->stream.num_channels,
					   e->stream.tracks);
			ra_stream_table_rx_set(&rx->sttb, &e->stream,
					       cmd.index, e->trtb_index, false);

			ret = aret;
			goto out_unlock;
		}

		e->trtb_index = ret;
	}

	/* We need to flush the previous hash table entry in case the IP/port changes */
	if (e->stream.primary.destination_ip     != cmd.stream.primary.destination_ip     ||
	    e->stream.primary.destination_port   != cmd.stream.primary.destination_port   ||
	    e->stream.secondary.destination_ip   != cmd.stream.secondary.destination_ip   ||
	    e->stream.secondary.destination_port != cmd.stream.secondary.destination_port)
		invalidate = true;

	memcpy(&e->stream, &cmd.stream, sizeof(e->stream));

	ra_sd_rx_tracks_alloc(rx, &e->stream);
	ra_track_table_set(&rx->trtb, e->trtb_index,
			   e->stream.num_channels, e->stream.tracks);
	ra_stream_table_rx_set(&rx->sttb, &e->stream, cmd.index,
			       e->trtb_index, invalidate);

out_unlock:
	mutex_unlock(&rx->mutex);

	return ret;
}

static void ra_sd_rx_free_stream(struct ra_sd_rx *rx,
				 struct ra_sd_rx_stream_elem *e,
				 int index)
{
	dev_dbg(rx->dev, "Deleting RX stream %d", index);

	ra_track_table_free(&rx->trtb, e->trtb_index, e->stream.num_channels);
	ra_stream_table_rx_del(&rx->sttb, index);
	ra_sd_rx_tracks_free(rx, &e->stream);
	xa_erase(&rx->streams, index);
	put_pid(e->pid);
	kfree(e);
}

int ra_sd_rx_delete_stream_ioctl(struct ra_sd_rx *rx, struct file *filp,
				 unsigned int size, void __user *buf)
{
	struct ra_sd_delete_rx_stream_cmd cmd;
	struct ra_sd_rx_stream_elem *e;
	int ret = 0;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	mutex_lock(&rx->mutex);

	e = ra_sd_rx_stream_elem_find_by_index(rx, cmd.index);
	if (!e) {
		dev_dbg(rx->dev, "Failed to find RX stream with index %d",
			cmd.index);
		ret = -ENOENT;
		goto out_unlock;
	}

	/* Streams can only be torn down by their creators */
	if (e->filp != filp) {
		ret = -EACCES;
		goto out_unlock;
	}

	ra_sd_rx_free_stream(rx, e, cmd.index);

out_unlock:
	mutex_unlock(&rx->mutex);

	return ret;
}

int ra_sd_rx_delete_streams(struct ra_sd_rx *rx, struct file *filp)
{
	unsigned long index;
	struct ra_sd_rx_stream_elem *e;

	mutex_lock(&rx->mutex);

	/* Remove all streams the client has created */
	xa_for_each(&rx->streams, index, e)
		if (e->filp == filp)
			ra_sd_rx_free_stream(rx, e, index);

	mutex_unlock(&rx->mutex);

	return 0;
}

static void ra_sd_rx_destroy_streams(void *xa)
{
	BUG_ON(!xa_empty(xa));
	xa_destroy(xa);
}

int ra_sd_rx_probe(struct ra_sd_rx *rx, struct device *dev)
{
	struct device_node *child_node;
	int ret;

	rx->dev = dev;
	mutex_init(&rx->mutex);

	child_node = of_parse_phandle(dev->of_node, "stream-table-rx", 0);
	if (!child_node) {
		dev_err(dev, "No stream-table-rx node");
		return -ENODEV;
	}

	ret = ra_stream_table_rx_probe(dev, child_node, &rx->sttb);
	of_node_put(child_node);
	if (ret < 0)
		return ret;

	child_node = of_parse_phandle(dev->of_node, "track-table-rx", 0);
	if (!child_node) {
		dev_err(dev, "No track-table-rx node");
		return -ENODEV;
	}

	ret = ra_track_table_probe(dev, child_node, &rx->trtb);
	of_node_put(child_node);
	if (ret < 0)
		return ret;

	xa_init_flags(&rx->streams, XA_FLAGS_ALLOC);
	ret = devm_add_action_or_reset(dev, ra_sd_rx_destroy_streams,
				       &rx->streams);
	if (ret < 0)
		return ret;

	rx->used_tracks = devm_bitmap_zalloc(dev, RA_MAX_TRACKS, GFP_KERNEL);
	if (!rx->used_tracks)
		return -ENOMEM;

	return 0;
}
