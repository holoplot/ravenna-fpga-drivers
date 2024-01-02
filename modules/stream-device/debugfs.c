// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <ravenna/version.h>

#include "main.h"
#include "rx.h"
#include "track-table.h"
#include "tx.h"

static int ra_sd_info_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	seq_printf(s, "Driver version: %s\n", ra_driver_version());
	seq_printf(s, "Device name: %s\n", priv->misc.name);
	seq_printf(s, "Device minor: %u\n", priv->misc.minor);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_info);

static int ra_sd_decoder_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	seq_printf(s, "RX decoder data dropped counter: %u\n",
		   ra_sd_ior(priv, RA_SD_CNT_RX_DEC_DROP));
	seq_printf(s, "RX decoder FIFO overflow counter: %u\n",
		   ra_sd_ior(priv, RA_SD_CNT_RX_DEC_FIFO_OVR));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_decoder);

/* Track table */

static void ra_sd_track_table_dump(struct ra_track_table *trtb,
				   struct seq_file *s)
{
	const int width = 16;
	int i;

	seq_puts(s, "          ");

	for (i = 0; i < width; i++)
		seq_printf(s, " 0x%02x", i);

	seq_puts(s, "\n");
	seq_puts(s, "-----------");

	for (i = 0; i < width; i++)
		seq_puts(s, "-----");

	seq_puts(s, "\n");

	for (i = 0; i < trtb->max_entries; i++) {
		u32 track = ra_track_table_read(trtb, i);

		if (i % width == 0)
			seq_printf(s, "  0x%03x | ", i);


		if (test_bit(i, trtb->used_entries))
			if (track & RA_TRACK_TABLE_MUTE)
				seq_printf(s, "  M  ");
			else
				seq_printf(s, " %3d ", track);
		else
			seq_puts(s, "  -  ");

		if (i % width == width-1)
			seq_puts(s, "\n");
	}

	seq_puts(s, "\n");
}

/* TX */

static void ra_sd_dump_tracks(struct seq_file *s,
			      const __s16 *tracks,
			      int num_channels)
{
	int i, j = 0;

	seq_printf(s, "  Channel -> Track mapping:");

	ra_for_each_active_track(i, num_channels, tracks) {
		if (j++ % 8 == 0)
			seq_puts(s, "\n    ");

		seq_printf(s, "   %3d -> %3d", i, tracks[i]);
	}

	seq_puts(s, "\n");
}

static int ra_sd_tx_summary_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;
	struct ra_sd_tx_stream_elem *e;
	unsigned long index;
	int count = 0;

	mutex_lock(&priv->tx.mutex);

	xa_for_each(&priv->tx.streams, index, e)
		count++;

	seq_printf(s, "Streams: %u/%u\n", count, priv->tx.sttb.max_entries);
	seq_printf(s, "Track table entries: %u/%u\n",
		   bitmap_weight(priv->tx.trtb.used_entries,
				 priv->tx.trtb.max_entries),
		   priv->tx.trtb.max_entries);

	mutex_unlock(&priv->tx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_tx_summary);

static void ra_sd_tx_print_interface(struct seq_file *s,
				     struct ra_sd_tx_stream_interface *i,
				     bool vlan)
{
	seq_printf(s, "    Source: %pI4:%u\n",
		&i->source_ip, be16_to_cpu(i->source_port));
	seq_printf(s, "    Destination: %pI4:%u\n",
		&i->destination_ip, be16_to_cpu(i->destination_port));
	seq_printf(s, "    Destination MAC: %pM\n", &i->destination_mac);

	if (vlan)
		seq_printf(s, "    VLAN tag: %u\n", be16_to_cpu(i->vlan_tag));
}

static int ra_sd_tx_streams_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;
	struct ra_sd_tx_stream_elem *e;
	unsigned long index;

	mutex_lock(&priv->tx.mutex);

	xa_for_each(&priv->tx.streams, index, e) {
		struct ra_sd_tx_stream *st = &e->stream;

		seq_printf(s, "Stream #%lu\n", index);

		seq_printf(s, "  Created by: PID %u\n", pid_vnr(e->pid));

		if (st->use_primary) {
			seq_printf(s, "  Primary network\n");
			ra_sd_tx_print_interface(s, &st->primary, st->vlan_tagged);
		}

		if (st->use_secondary) {
			seq_printf(s, "  Secondary network\n");
			ra_sd_tx_print_interface(s, &st->secondary, st->vlan_tagged);
		}

		seq_printf(s, "  Channels: %u\n", st->num_channels);
		seq_printf(s, "  Samples: %u\n", st->num_samples);

		seq_printf(s, "  Codec: %s\n", ra_sd_codec_str(st->codec));
		seq_printf(s, "  RTP payload type: %u\n", st->rtp_payload_type);
		seq_printf(s, "  RTP offset: %u\n", st->rtp_offset);
		seq_printf(s, "  RTP SSRC: %u\n", st->rtp_ssrc);

		seq_printf(s, "  Mode: %s%s\n",
			   st->vlan_tagged	? "VLAN-TAGGED " : "",
			   st->multicast	? "MULTICAST "   : "UNICAST");

		seq_printf(s, "  Track table start index: %u\n", e->trtb_index);

		ra_sd_dump_tracks(s, st->tracks, st->num_channels);

		seq_puts(s, "\n");
	}

	mutex_unlock(&priv->tx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_tx_streams);

static int ra_sd_tx_stream_table_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	mutex_lock(&priv->tx.mutex);
	ra_stream_table_tx_dump(&priv->tx.sttb, s);
	mutex_unlock(&priv->tx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_tx_stream_table);

static int ra_sd_tx_track_table_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	mutex_lock(&priv->tx.mutex);
	ra_sd_track_table_dump(&priv->tx.trtb, s);
	mutex_unlock(&priv->tx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_tx_track_table);

/* RX */

static int ra_sd_rx_summary_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;
	struct ra_sd_rx_stream_elem *e;
	unsigned long index;
	int count = 0;

	mutex_lock(&priv->rx.mutex);

	xa_for_each(&priv->rx.streams, index, e)
		count++;

	seq_printf(s, "Streams: %u/%u\n", count, priv->rx.sttb.max_entries);
	seq_printf(s, "Track table entries: %u/%u\n",
		   bitmap_weight(priv->rx.trtb.used_entries,
				 priv->rx.trtb.max_entries),
		   priv->rx.trtb.max_entries);
	seq_printf(s, "Tracks: %u/%u\n",
		   bitmap_weight(priv->rx.used_tracks, priv->max_tracks),
		   priv->max_tracks);

	mutex_unlock(&priv->rx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_rx_summary);

static int ra_sd_rx_streams_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;
	struct ra_sd_rx_stream_elem *e;
	unsigned long index;

	mutex_lock(&priv->rx.mutex);

	xa_for_each(&priv->rx.streams, index, e) {
		struct ra_sd_rx_stream *st = &e->stream;

		seq_printf(s, "Stream #%lu (%s)\n", index,
			   st->active ? "ACTIVE" : "INACTIVE");

		seq_printf(s, "  Created by: PID %u\n", pid_vnr(e->pid));

		if (st->primary.destination_ip != 0)
			seq_printf(s, "  Primary destination: %pI4:%u\n",
				   &st->primary.destination_ip,
				   be16_to_cpu(st->primary.destination_port));

		if (st->secondary.destination_ip != 0)
			seq_printf(s, "  Secondary destination: %pI4:%u\n",
				   &st->secondary.destination_ip,
				   be16_to_cpu(st->secondary.destination_port));

		if (st->vlan_tagged)
			seq_printf(s, "  VLAN tag: %u\n",
				   be16_to_cpu(st->vlan_tag));

		seq_printf(s, "  Channels: %u\n", st->num_channels);
		seq_printf(s, "  Codec: %s\n", ra_sd_codec_str(st->codec));
		seq_printf(s, "  RTP payload type: %u\n", st->rtp_payload_type);
		seq_printf(s, "  RTP offset: %u\n", st->rtp_offset);
		seq_printf(s, "  RTP SSRC: %u\n", st->rtp_ssrc);
		seq_printf(s, "  Jitter buffer margin: %u\n", st->jitter_buffer_margin);

		seq_printf(s, "  Mode: %s%s%s%s%s\n",
			   st->sync_source		? "SYNC-SOURCE " : "",
			   st->vlan_tagged		? "VLAN-TAGGED " : "",
			   st->hitless_protection	? "HITLESS " 	 : "",
			   st->rtp_filter		? "RTP-FILTER "  : "",
			   st->synchronous		? "SYNCHRONOUS " :
							  "SYNTONOUS ");

		seq_printf(s, "  Track table start index: %u\n", e->trtb_index);

		ra_sd_dump_tracks(s, st->tracks, st->num_channels);

		seq_puts(s, "\n");
	}

	mutex_unlock(&priv->rx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_rx_streams);

static int ra_sd_rx_stream_table_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	mutex_lock(&priv->rx.mutex);
	ra_stream_table_rx_dump(&priv->rx.sttb, s);
	mutex_unlock(&priv->rx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_rx_stream_table);

static int ra_sd_rx_track_table_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;

	mutex_lock(&priv->rx.mutex);
	ra_sd_track_table_dump(&priv->rx.trtb, s);
	mutex_unlock(&priv->rx.mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_rx_track_table);

static int ra_sd_rx_hash_table_show(struct seq_file *s, void *p)
{
	struct ra_sd_priv *priv = s->private;
	u32 val = ra_sd_ior(priv, RA_SD_RX_HSTB_STAT);

	seq_printf(s, "Hash table entries: %u\n", val & 0xff);
	seq_printf(s, "Large clusters: %u\n", (val >> 8) & 0xff);
	seq_printf(s, "Maximum cluster length: %u\n", (val >> 16) & 0xff);
	seq_printf(s, "Fragmented entries: %u\n", (val >> 24) & 0xff);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ra_sd_rx_hash_table);

static void ra_sd_remove_debugfs(void *root)
{
	debugfs_remove_recursive(root);
}

int ra_sd_debugfs_init(struct ra_sd_priv *priv)
{
	int ret;
	struct dentry *rx, *tx;

	priv->debugfs = debugfs_create_dir(priv->misc.name, NULL);
	if (IS_ERR(priv->debugfs))
		return PTR_ERR(priv->debugfs);

	ret = devm_add_action_or_reset(priv->dev, ra_sd_remove_debugfs, priv->debugfs);
	if (ret < 0)
		return ret;

	debugfs_create_file("info", 0444, priv->debugfs, priv, &ra_sd_info_fops);

	rx = debugfs_create_dir("rx", priv->debugfs);
	if (IS_ERR(rx))
		return PTR_ERR(rx);

	debugfs_create_file("summary", 0444, rx, priv, &ra_sd_rx_summary_fops);
	debugfs_create_file("decoder", 0444, rx, priv, &ra_sd_decoder_fops);
	debugfs_create_file("streams", 0444, rx, priv, &ra_sd_rx_streams_fops);
	debugfs_create_file("stream-table", 0444, rx, priv, &ra_sd_rx_stream_table_fops);
	debugfs_create_file("track-table", 0444, rx, priv, &ra_sd_rx_track_table_fops);
	debugfs_create_file("hash-table", 0444, rx, priv, &ra_sd_rx_hash_table_fops);

	tx = debugfs_create_dir("tx", priv->debugfs);
	if (IS_ERR(tx))
		return PTR_ERR(tx);

	debugfs_create_file("summary", 0444, tx, priv, &ra_sd_tx_summary_fops);
	debugfs_create_file("streams", 0444, tx, priv, &ra_sd_tx_streams_fops);
	debugfs_create_file("stream-table", 0444, tx, priv, &ra_sd_tx_stream_table_fops);
	debugfs_create_file("track-table", 0444, tx, priv, &ra_sd_tx_track_table_fops);

	return 0;
}
