// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_address.h>

#include "codec.h"
#include "stream-table-tx.h"

/* misc_control */
#define RA_STREAM_TABLE_TX_MISC_VLD		BIT(7)
#define RA_STREAM_TABLE_TX_MISC_ACT		BIT(6)
#define RA_STREAM_TABLE_TX_MISC_VLAN		BIT(4)
#define RA_STREAM_TABLE_TX_MISC_MULTICAST	BIT(3)
#define RA_STREAM_TABLE_TX_MISC_SEC		BIT(1)
#define RA_STREAM_TABLE_TX_MISC_PRI		BIT(0)

struct ra_stream_table_tx_fpga {
#ifdef __LITTLE_ENDIAN
	__u16 trtp_base_addr;			/* 0x00 */
	__u8 codec;				/* 0x02 */
	__u8 misc_control;			/* 0x03 */

	__u8 num_channels;			/* 0x04 */
	__u8 reserved_1;			/* 0x05 */
	__u8 num_samples;			/* 0x06 */
	__u8 reserved_0;			/* 0x07 */

	__u32 destination_ip_primary;		/* 0x08 */
	__u32 destination_ip_secondary;		/* 0x0c */

	__u32 destination_mac_primary_msb;	/* 0x10 */
	__u16 destination_mac_secondary_msb;	/* 0x14 */
	__u16 destination_mac_primary_lsb;	/* 0x16 */
	__u32 destination_mac_secondary_lsb;	/* 0x18 */

	__u16 vlan_tag_primary;			/* 0x1c */
	__u16 vlan_tag_secondary;		/* 0x1e */

	__u16 ip_total_len;			/* 0x20 */
	__u8 dscp_tos;				/* 0x22 */
	__u8 ttl;				/* 0x23 */

	__u32 source_ip_primary;		/* 0x24 */
	__u32 source_ip_secondary;		/* 0x28 */

	__u16 destination_port_primary;		/* 0x2c */
	__u16 source_port_primary;		/* 0x2e */
	__u16 destination_port_secondary;	/* 0x30 */
	__u16 source_port_secondary;		/* 0x32 */

	__u8 next_rtp_tx_time;			/* 0x34 */
	__u8 rtp_payload_type;			/* 0x35 */
	__u16 next_rtp_sequence_num;		/* 0x36 */

	__u32 rtp_offset;			/* 0x38 */
	__u32 rtp_ssrc;				/* 0x3c */
#else /* __BIG_ENDIAN */
#error Big Endian platforms are unsupported
#endif
} __packed;

static inline
void ra_stream_table_tx_stream_write(struct ra_stream_table_tx *sttb,
				     struct ra_stream_table_tx_fpga *fpga,
				     int index)
{
	void __iomem *dest = sttb->regs + sizeof(*fpga) * index;

	BUILD_BUG_ON(!IS_ALIGNED(sizeof(*fpga), sizeof(u32)));
	BUG_ON(index >= sttb->max_entries);

	__iowrite32_copy(dest, fpga, sizeof(*fpga) / sizeof(u32));
	cpu_relax();
}

static inline
void ra_stream_table_tx_stream_read(struct ra_stream_table_tx *sttb,
				    struct ra_stream_table_tx_fpga *fpga,
				    int index)
{
	void __iomem *src = sttb->regs + sizeof(*fpga) * index;

	BUG_ON(index >= sttb->max_entries);
	__ioread32_copy(fpga, src, sizeof(*fpga) / sizeof(u32));
}

static void ra_stream_table_tx_fill(const struct ra_sd_tx_stream *stream,
				    struct ra_stream_table_tx_fpga *fpga,
				    int trtb_index, int ip_total_len)
{
	const struct ra_sd_tx_stream_interface *pri = &stream->primary;
	const struct ra_sd_tx_stream_interface *sec = &stream->secondary;

	memset(fpga, 0, sizeof(*fpga));

	if (stream->vlan_tagged)
		fpga->misc_control |= RA_STREAM_TABLE_TX_MISC_VLAN;

	if (stream->multicast)
		fpga->misc_control |= RA_STREAM_TABLE_TX_MISC_MULTICAST;

	if (stream->use_primary)
		fpga->misc_control |= RA_STREAM_TABLE_TX_MISC_PRI;

	if (stream->use_secondary)
		fpga->misc_control |= RA_STREAM_TABLE_TX_MISC_SEC;

	fpga->codec = ra_sd_codec_fpga_code(stream->codec);

	fpga->ip_total_len = ip_total_len;

	fpga->trtp_base_addr = trtb_index;

	fpga->num_channels = stream->num_channels;
	fpga->num_samples = stream->num_samples;

	fpga->destination_ip_primary = be32_to_cpu(pri->destination_ip);
	fpga->destination_ip_secondary = be32_to_cpu(sec->destination_ip);
	fpga->source_ip_primary = be32_to_cpu(pri->source_ip);
	fpga->source_ip_secondary = be32_to_cpu(sec->source_ip);

	fpga->source_port_primary = be16_to_cpu(pri->source_port);
	fpga->source_port_secondary = be16_to_cpu(sec->source_port);
	fpga->destination_port_primary = be16_to_cpu(pri->destination_port);
	fpga->destination_port_secondary = be16_to_cpu(sec->destination_port);

	fpga->destination_mac_primary_msb =
		pri->destination_mac[0] << 24 |
		pri->destination_mac[1] << 16 |
		pri->destination_mac[2] << 8 |
		pri->destination_mac[3] << 0;
	fpga->destination_mac_primary_lsb =
		pri->destination_mac[4] << 8 |
		pri->destination_mac[5] << 0;

	fpga->destination_mac_secondary_msb =
		sec->destination_mac[0] << 24 |
		sec->destination_mac[1] << 16 |
		sec->destination_mac[2] << 8 |
		sec->destination_mac[3] << 0;
	fpga->destination_mac_secondary_lsb =
		sec->destination_mac[4] << 8 |
		sec->destination_mac[5] << 0;

	fpga->vlan_tag_primary = be16_to_cpu(pri->vlan_tag);
	fpga->vlan_tag_secondary = be16_to_cpu(sec->vlan_tag);

	fpga->ttl = stream->ttl;
	fpga->dscp_tos = stream->dscp_tos;

	fpga->next_rtp_sequence_num = stream->next_rtp_sequence_num;
	fpga->rtp_payload_type = stream->rtp_payload_type;
	fpga->next_rtp_tx_time = stream->next_rtp_tx_time;

	fpga->rtp_offset = stream->rtp_offset;
	fpga->rtp_ssrc = stream->rtp_ssrc;
}

void ra_stream_table_tx_set(struct ra_stream_table_tx *sttb,
			    struct ra_sd_tx_stream *stream,
			    int index, int trtb_index,
			    int ip_total_len,
			    bool invalidate)
{
	struct ra_stream_table_tx_fpga fpga = { 0 };

	ra_stream_table_tx_fill(stream, &fpga, trtb_index, ip_total_len);

	if (invalidate)
		ra_stream_table_tx_stream_write(sttb, &fpga, index);

	fpga.misc_control |=
		RA_STREAM_TABLE_TX_MISC_VLD |
		RA_STREAM_TABLE_TX_MISC_ACT;

	ra_stream_table_tx_stream_write(sttb, &fpga, index);
}

void ra_stream_table_tx_del(struct ra_stream_table_tx *sttb, int index)
{
	struct ra_stream_table_tx_fpga fpga = { 0 };

	ra_stream_table_tx_stream_write(sttb, &fpga, index);
}

static void ra_stream_table_tx_reset(struct ra_stream_table_tx *sttb)
{
	int i;

	for (i = 0; i < sttb->max_entries; i++)
		ra_stream_table_tx_del(sttb, i);
}

void ra_stream_table_tx_dump(struct ra_stream_table_tx *sttb,
			     struct seq_file *s)
{
	int i;

	for (i = 0; i < sttb->max_entries; i++) {
		struct ra_stream_table_tx_fpga fpga;

		ra_stream_table_tx_stream_read(sttb, &fpga, i);

		seq_printf(s, "Entry #%d (%s, %s)\n", i,
			(fpga.misc_control & RA_STREAM_TABLE_TX_MISC_VLD) ?
				"VALID" : "INVALID",
			(fpga.misc_control & RA_STREAM_TABLE_TX_MISC_ACT) ?
				"ACTIVE" : "INACTIVE");

		seq_hex_dump(s, "  ", DUMP_PREFIX_OFFSET, 16, 1,
			     &fpga, sizeof(fpga), true);
		seq_puts(s, "\n");
	}
}

int ra_stream_table_tx_probe(struct device *dev,
			     struct device_node *np,
			     struct ra_stream_table_tx *sttb)
{
	resource_size_t size;
	struct resource res;
	int ret;

	BUILD_BUG_ON(sizeof(struct ra_stream_table_tx_fpga) != 0x40);

	ret = of_address_to_resource(np, 0, &res);
	if (ret < 0) {
		dev_err(dev, "Failed to access TX stream table: %d", ret);
		return ret;
	}

	size = resource_size(&res);

	if (!IS_ALIGNED(size, sizeof(struct ra_stream_table_tx_fpga))) {
		dev_err(dev, "Invalid resource size for TX stream table");
		return -EINVAL;
	}

	sttb->regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(sttb->regs)) {
		dev_err(dev, "Failed to map resource for TX stream table");
		return PTR_ERR(sttb->regs);
	}

	sttb->max_entries = size / sizeof(struct ra_stream_table_tx_fpga);

	ra_stream_table_tx_reset(sttb);

	dev_info(dev, "TX stream table, %d entries", sttb->max_entries);

	return 0;
}
