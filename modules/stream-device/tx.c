// SPDX-License-Identifier: GPL-2.0-or-later

#define DEBUG 1

#include <linux/of.h>

#include "main.h"
#include "rtp.h"

static struct ra_sd_tx_stream_elem *
ra_sd_tx_stream_elem_find_by_index(struct ra_sd_tx *tx, int index)
{
	struct ra_sd_tx_stream_elem *e = xa_load(&tx->streams, index);

	return xa_is_err(e) ? NULL : e;
}

static int
ra_sd_rx_validate_stream_interface(const struct ra_sd_tx_stream_interface *iface)
{
	if (iface->destination_ip == 0		||
	    iface->destination_port == 0	||
	    iface->source_ip == 0 		||
	    iface->source_port == 0)
		return -EINVAL;

	if (iface->vlan_tagged && be16_to_cpu(iface->vlan_tag) > 4095)
		return -EINVAL;

	return 0;
}

static int ra_sd_tx_validate_stream(const struct ra_sd_tx_stream *stream)
{
	int i, ret;

	if (!stream->use_primary &&
	    !stream->use_secondary)
		return -EINVAL;

	if (stream->use_primary) {
		ret = ra_sd_rx_validate_stream_interface(&stream->primary);
		if (ret < 0)
			return ret;
	}

	if (stream->use_secondary) {
		ret = ra_sd_rx_validate_stream_interface(&stream->secondary);
		if (ret < 0)
			return ret;
	}

	if (stream->dscp_tos >= 64)
		return -EINVAL;

	if (stream->rtp_ssrc == 0)
		return -EINVAL;

	ret = ra_sd_validate_rtp_payload_type(stream->rtp_payload_type,
					      stream->num_channels,
					      stream->codec);
	if (ret < 0)
		return ret;

	if (stream->codec >= _RA_STREAM_CODEC_MAX)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(stream->tracks); i++)
		if (stream->tracks[i] >= RA_MAX_TRACKS)
			return -EINVAL;

	return 0;
}

static int ra_sd_tx_stream_ip_length(const struct ra_sd_tx_stream *stream)
{
	int codec_len, payload_len;

	codec_len = ra_sd_codec_sample_length(stream->codec);
	payload_len = stream->num_channels * stream->num_samples * codec_len;

	// 20 bytes IP header + 8 bytes UDP header + 12 bytes RTP header + RTP data
	return 20 + 8 + 12 + payload_len;
}

int ra_sd_tx_add_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
			      unsigned int size, void __user *buf)
{
	struct ra_sd_add_tx_stream_cmd cmd;
	struct ra_sd_tx_stream_elem *e;
	int ip_total_len;
	u32 index;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	ret = ra_sd_tx_validate_stream(&cmd.stream);
	if (ret < 0)
		return ret;

	ip_total_len = ra_sd_tx_stream_ip_length(&cmd.stream);
	if (ip_total_len > RA_MAX_ETHERNET_PACKET_SIZE)
		return -EINVAL;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	e->filp = filp;
	e->pid = get_pid(task_pid(current));
	memcpy(&e->stream, &cmd.stream, sizeof(e->stream));

	mutex_lock(&tx->mutex);

	ret = xa_alloc(&tx->streams, &index, e,
		       XA_LIMIT(0, tx->sttb.max_entries-1), GFP_KERNEL);
	if (ret < 0) {
		dev_err(tx->dev, "xa_alloc() failed: %d\n", ret);
		goto out_unlock;
	}

	ret = ra_track_table_alloc(&tx->trtb, e->stream.num_channels);
	if (ret < 0) {
		dev_err(tx->dev, "ra_track_table_alloc() failed: %d\n", ret);
		xa_erase(&tx->streams, index);
		goto out_unlock;
	}

	e->trtb_index = ret;

	ra_track_table_set(&tx->trtb, e->trtb_index,
			   e->stream.num_channels, e->stream.tracks);
	ra_stream_table_tx_set(&tx->sttb, &e->stream,
				index, e->trtb_index, ip_total_len, true);

	dev_dbg(tx->dev, "Added TX stream with index %d", index);

out_unlock:
	mutex_unlock(&tx->mutex);

	if (ret < 0) {
		kfree(e);
		return ret;
	}

	return index;
}

int ra_sd_tx_update_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
				 unsigned int size, void __user *buf)
{
	struct ra_sd_update_tx_stream_cmd cmd;
	struct ra_sd_tx_stream_elem *e;
	int ip_total_len;
	int ret;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	ret = ra_sd_tx_validate_stream(&cmd.stream);
	if (ret < 0)
		return ret;

	ip_total_len = ra_sd_tx_stream_ip_length(&cmd.stream);
	if (ip_total_len > RA_MAX_ETHERNET_PACKET_SIZE)
		return -EINVAL;

	mutex_lock(&tx->mutex);

	e = ra_sd_tx_stream_elem_find_by_index(tx, cmd.index);
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
		ra_track_table_free(&tx->trtb, e->trtb_index,
				    e->stream.num_channels);
		ret = ra_track_table_alloc(&tx->trtb, cmd.stream.num_channels);
		if (ret < 0) {
			int aret = ret;

			dev_err(tx->dev, "ra_track_table_alloc() failed: %d\n", ret);

			/* Roll back */
			ret = ra_track_table_alloc(&tx->trtb,
						   e->stream.num_channels);
			/*
			 * Can't really happen because the allocation was
			 * valid before.
			 */
			if (WARN_ON(ret < 0))
				goto out_unlock;

			e->trtb_index = ret;
			ra_track_table_set(&tx->trtb, e->trtb_index,
					   e->stream.num_channels,
					   e->stream.tracks);
			ra_stream_table_tx_set(&tx->sttb, &e->stream,
					       cmd.index, e->trtb_index,
					       ra_sd_tx_stream_ip_length(&e->stream),
					       false);

			ret = aret;
			goto out_unlock;
		}

		e->trtb_index = ret;
	}

	memcpy(&e->stream, &cmd.stream, sizeof(e->stream));

	ra_track_table_set(&tx->trtb, e->trtb_index,
			   e->stream.num_channels, e->stream.tracks);
	ra_stream_table_tx_set(&tx->sttb, &e->stream, cmd.index,
			       e->trtb_index, ip_total_len, false);

out_unlock:
	mutex_unlock(&tx->mutex);

	return ret;
}

static void ra_sd_tx_free_stream(struct ra_sd_tx *tx,
				 struct ra_sd_tx_stream_elem *e,
				 int index)
{
	dev_dbg(tx->dev, "Deleting stream %d", index);

	ra_track_table_free(&tx->trtb, e->trtb_index, e->stream.num_channels);
	ra_stream_table_tx_del(&tx->sttb, index);
	xa_erase(&tx->streams, index);
	put_pid(e->pid);
	kfree(e);
}

int ra_sd_tx_delete_stream_ioctl(struct ra_sd_tx *tx, struct file *filp,
			      unsigned int size, void __user *buf)
{
	struct ra_sd_delete_tx_stream_cmd cmd;
	struct ra_sd_tx_stream_elem *e;
	int ret = 0;

	if (size != sizeof(cmd))
		return -EINVAL;

	if (copy_from_user(&cmd, buf, sizeof(cmd)))
		return -EFAULT;

	if (cmd.version != 0)
		return -EINVAL;

	mutex_lock(&tx->mutex);

	e = ra_sd_tx_stream_elem_find_by_index(tx, cmd.index);
	if (!e) {
		dev_dbg(tx->dev, "Failed to find TX stream with index %d",
			cmd.index);
		ret = -ENOENT;
		goto out_unlock;
	}

	/* Streams can only be torn down by their creators */
	if (e->filp != filp) {
		ret = -EACCES;
		goto out_unlock;
	}

	ra_sd_tx_free_stream(tx, e, cmd.index);

out_unlock:
	mutex_unlock(&tx->mutex);

	return ret;
}

int ra_sd_tx_delete_streams(struct ra_sd_tx *tx, struct file *filp)
{
	struct ra_sd_tx_stream_elem *e;
	unsigned long index;

	mutex_lock(&tx->mutex);

	/* Remove all streams the client has created */
	xa_for_each(&tx->streams, index, e)
		if (e->filp == filp)
			ra_sd_tx_free_stream(tx, e, index);

	mutex_unlock(&tx->mutex);

	return 0;
}

static void ra_sd_tx_destroy_streams(void *xa)
{
	BUG_ON(!xa_empty(xa));
	xa_destroy(xa);
}

int ra_sd_tx_probe(struct ra_sd_tx *tx, struct device *dev)
{
	struct device_node *child_node;
	int ret;

	tx->dev = dev;
	mutex_init(&tx->mutex);

	child_node = of_parse_phandle(dev->of_node, "stream-table-tx", 0);
	if (!child_node) {
		dev_err(dev, "No stream-table-tx node");
		return -ENODEV;
	}

	ret = ra_stream_table_tx_probe(dev, child_node, &tx->sttb);
	of_node_put(child_node);
	if (ret < 0)
		return ret;

	child_node = of_parse_phandle(dev->of_node, "track-table-tx", 0);
	if (!child_node) {
		dev_err(dev, "No track-table-tx node");
		return -ENODEV;
	}

	ret = ra_track_table_probe(dev, child_node, &tx->trtb);
	of_node_put(child_node);
	if (ret < 0)
		return ret;

	xa_init_flags(&tx->streams, XA_FLAGS_ALLOC);
	ret = devm_add_action_or_reset(dev, ra_sd_tx_destroy_streams,
				       &tx->streams);
	if (ret < 0)
		return ret;

	return 0;
}
