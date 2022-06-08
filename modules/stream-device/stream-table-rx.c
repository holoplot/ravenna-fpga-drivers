// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/of_platform.h>
#include <linux/of_address.h>

#include "codec.h"
#include "stream-table-rx.h"

#define RA_STREAM_TABLE_RX_MISC_VLD		BIT(7)
#define RA_STREAM_TABLE_RX_MISC_ACT		BIT(6)
#define RA_STREAM_TABLE_RX_MISC_SYNC_SOURCE	BIT(5)
#define RA_STREAM_TABLE_RX_MISC_VLAN		BIT(4)
#define RA_STREAM_TABLE_RX_MISC_EXEC_HASH	BIT(2)
#define RA_STREAM_TABLE_RX_MISC_HITLESS		BIT(1)
#define RA_STREAM_TABLE_RX_MISC_SYNCHRONOUS	BIT(0)

struct ra_stream_table_rx_fpga {
#ifdef __LITTLE_ENDIAN
	__be32 destination_ip_primary;		/* 0x00 */
	__be32 destination_ip_secondary;	/* 0x04 */

	__be16 destination_port_secondary;	/* 0x08 */
	__be16 destination_port_primary;	/* 0x0a */

	__u8 num_channels;			/* 0x0f */
	__u8 reserved_0;			/* 0x0e */
	__u8 codec;				/* 0x0d */
	__u8 misc_control;			/* 0x0c */

	__u32 rtp_offset;			/* 0x10 */

	__u16 jitter_buffer_margin;		/* 0x16 */
	__u16 trtp_base_addr;			/* 0x14 */

	__u32 rtp_ssrc;				/* 0x18 */

	__u8 rtp_payload_type;			/* 0x1f */
	__u8 rtcp_control;			/* 0x1e */
	__u16 rtp_filter_vlan_id;		/* 0x1c */
#else /* __BIG_ENDIAN */
	__be32 destination_ip_primary;		/* 0x00 */
	__be32 destination_ip_secondary;	/* 0x04 */

	__be16 destination_port_primary;	/* 0x0a */
	__be16 destination_port_secondary;	/* 0x08 */

	__u8 misc_control;			/* 0x0c */
	__u8 codec;				/* 0x0d */
	__u8 reserved_0;			/* 0x0e */
	__u8 num_channels;			/* 0x0f */

	__u32 rtp_offset;			/* 0x10 */

	__u16 trtp_base_addr;			/* 0x14 */
	__u16 jitter_buffer_margin;		/* 0x16 */

	__u32 rtp_ssrc;				/* 0x18 */

	__u16 rtp_filter_vlan_id;		/* 0x1c */
	__u8 rtcp_control;			/* 0x1e */
	__u8 rtp_payload_type;			/* 0x1f */
#endif
} __packed;

static inline
void ra_stream_table_rx_stream_write(struct ra_stream_table_rx *sttb,
				     struct ra_stream_table_rx_fpga *fpga,
				     int index)
{
	void __iomem *dest = sttb->regs + sizeof(*fpga) * index;

	BUILD_BUG_ON(!IS_ALIGNED(sizeof(*fpga), sizeof(u32)));
	BUG_ON(index >= sttb->max_entries);

	__iowrite32_copy(dest, fpga, sizeof(*fpga) / sizeof(u32));
	cpu_relax();
}

static inline
void ra_stream_table_rx_stream_read(struct ra_stream_table_rx *sttb,
				    struct ra_stream_table_rx_fpga *fpga,
				    int index)
{
	void __iomem *src = sttb->regs + sizeof(*fpga) * index;

	BUG_ON(index >= sttb->max_entries);
	__ioread32_copy(fpga, src, sizeof(*fpga) / sizeof(u32));
}

static void ra_stream_table_rx_fill(const struct ra_sd_rx_stream *stream,
				    struct ra_stream_table_rx_fpga *fpga,
				    int trtb_index)
{
	const struct ra_sd_rx_stream_interface *pri = &stream->primary;
	const struct ra_sd_rx_stream_interface *sec = &stream->secondary;

	memset(fpga, 0, sizeof(*fpga));

	fpga->destination_ip_primary = be32_to_cpu(pri->destination_ip);
	fpga->destination_ip_secondary = be32_to_cpu(sec->destination_ip);
	fpga->destination_port_secondary = be16_to_cpu(sec->destination_port);
	fpga->destination_port_primary = be16_to_cpu(pri->destination_port);

	/*
	 * Non-redundant stream records must have the same entries for
	 * PRI & SEC. Otherwise the hstb will be flooded with identical entries
	 * for hash(0,0).
	 */
	if (fpga->destination_ip_primary == 0)
		fpga->destination_ip_primary = fpga->destination_ip_secondary;

	if (fpga->destination_ip_secondary == 0)
		fpga->destination_ip_secondary = fpga->destination_ip_primary;

	fpga->num_channels = stream->num_channels;
	fpga->rtp_offset = stream->rtp_offset;
	fpga->trtp_base_addr = trtb_index;
	fpga->jitter_buffer_margin = stream->jitter_buffer_margin;
	fpga->rtp_ssrc = stream->rtp_ssrc;

	fpga->rtp_filter_vlan_id = be16_to_cpu(stream->vlan_tag) & 0x3f;
	if (stream->rtp_filter)
		fpga->rtp_filter_vlan_id |= BIT(15);

	fpga->codec = ra_sd_codec_fpga_code(stream->codec);

	if (stream->sync_source)
		fpga->misc_control |= RA_STREAM_TABLE_RX_MISC_SYNC_SOURCE;

	if (stream->vlan_tagged)
		fpga->misc_control |= RA_STREAM_TABLE_RX_MISC_VLAN;

	if (stream->hitless_protection)
		fpga->misc_control |= RA_STREAM_TABLE_RX_MISC_HITLESS;

	if (stream->synchronous)
		fpga->misc_control |= RA_STREAM_TABLE_RX_MISC_SYNCHRONOUS;
}

void ra_stream_table_rx_set(struct ra_stream_table_rx *sttb,
			    struct ra_sd_rx_stream *stream,
			    int index, int trtb_index,
			    bool invalidate)
{
	struct ra_stream_table_rx_fpga fpga;

	ra_stream_table_rx_fill(stream, &fpga, trtb_index);

	if (invalidate)
		ra_stream_table_rx_stream_write(sttb, &fpga, index);

	fpga.misc_control |=
		RA_STREAM_TABLE_RX_MISC_VLD |
		RA_STREAM_TABLE_RX_MISC_ACT |
		RA_STREAM_TABLE_RX_MISC_EXEC_HASH;

	ra_stream_table_rx_stream_write(sttb, &fpga, index);
}

void ra_stream_table_rx_del(struct ra_stream_table_rx *sttb, int index)
{
	struct ra_stream_table_rx_fpga fpga;

	ra_stream_table_rx_stream_read(sttb, &fpga, index);

	fpga.misc_control &= ~RA_STREAM_TABLE_RX_MISC_VLD;
	fpga.misc_control &= ~RA_STREAM_TABLE_RX_MISC_ACT;
	fpga.misc_control |=  RA_STREAM_TABLE_RX_MISC_EXEC_HASH;

	ra_stream_table_rx_stream_write(sttb, &fpga, index);
}

static void ra_stream_table_rx_reset(struct ra_stream_table_rx *sttb)
{
	int i;
	struct ra_stream_table_rx_fpga fpga = { 0 };

	for (i = 0; i < sttb->max_entries; i++)
		ra_stream_table_rx_stream_write(sttb, &fpga, i);
}

void ra_stream_table_rx_dump(struct ra_stream_table_rx *sttb,
			     struct seq_file *s)
{
	int i;

	for (i = 0; i < sttb->max_entries; i++) {
		struct ra_stream_table_rx_fpga fpga;

		ra_stream_table_rx_stream_read(sttb, &fpga, i);

		seq_printf(s, "Entry #%d (%s, %s)\n", i,
			(fpga.misc_control & RA_STREAM_TABLE_RX_MISC_VLD) ?
				"VALID" : "INVALID",
			(fpga.misc_control & RA_STREAM_TABLE_RX_MISC_ACT) ?
				"ACTIVE" : "INACTIVE");

		seq_hex_dump(s, "  ", DUMP_PREFIX_OFFSET, 16, 1,
			     &fpga, sizeof(fpga), true);
		seq_puts(s, "\n");
	}
}

int ra_stream_table_rx_probe(struct device *dev,
			     struct device_node *np,
			     struct ra_stream_table_rx *sttb)
{
	int ret;
	struct resource res;
	resource_size_t size;

	BUILD_BUG_ON(sizeof(struct ra_stream_table_rx_fpga) != 0x20);

	ret = of_address_to_resource(np, 0, &res);
	if (ret < 0) {
		dev_err(dev, "Failed to access stream table resource: %d", ret);
		return ret;
	}

	size = resource_size(&res);

	if (!IS_ALIGNED(size, sizeof(struct ra_stream_table_rx_fpga))) {
		dev_err(dev, "Invalid resource size for RX stream table");
		return -EINVAL;
	}

	sttb->max_entries = size / sizeof(struct ra_stream_table_rx_fpga);

	sttb->regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(sttb->regs)) {
		dev_err(dev, "Failed to map resource for stream table");
		return PTR_ERR(sttb->regs);
	}

	ra_stream_table_rx_reset(sttb);

	dev_info(dev, "Ravenna stream table RX, %d entries",
		 sttb->max_entries);

	return 0;
}
