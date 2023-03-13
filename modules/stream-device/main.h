// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SD_MAIN_H
#define RA_SD_MAIN_H

#include <linux/io.h>
#include <linux/miscdevice.h>

#include <uapi/ravenna/stream-device.h>

#include "codec.h"
#include "rx.h"
#include "tx.h"
#include "rtcp.h"

#define RA_SD_CONFIG			0x000
#define RA_SD_CONFIG_RTCP_TX		BIT(16)
#define RA_SD_CONFIG_RTCP_RX		BIT(0)

#define RA_SD_IRQ_REQUEST		0x004
#define RA_SD_IRQ_MASK			0x008
#define RA_SD_IRQ_RTCP_TX		BIT(16)
#define RA_SD_IRQ_RTCP_RX		BIT(0)

#define RA_SD_RX_PAGE_SELECT		0x00c
#define RA_SD_TX_PAGE_SELECT		0x014
#define RA_SD_COUNTER_RESET		0x020
#define RA_SD_CNT_RX_DEC_DROP		0x024
#define RA_SD_CNT_RX_DEC_FIFO_OVR	0x028
#define RA_SD_RX_HSTB_STAT		0x02c /* R */
#define RA_SD_RX_HSTB_CLEAR		0x02c /* W */

#define RA_SD_RTCP_RX_DATA		0x100
#define RA_SD_RTCP_TX_DATA		0x180

struct ra_sd_priv {
	struct device		*dev;
	struct miscdevice	misc;
	void __iomem		*regs;
	struct dentry		*debugfs;

	struct {
		wait_queue_head_t		wait;
		struct mutex			mutex;
		bool				ready;
		struct ra_sd_rtcp_tx_data_fpga	data;
	} rtcp_tx;

	struct {
		wait_queue_head_t	wait;
		struct mutex		mutex;
		bool			ready;
		struct ra_sd_rtcp_rx_data_fpga	data;
	} rtcp_rx;

	struct ra_sd_rx rx;
	struct ra_sd_tx tx;
};

#define to_ra_sd_priv(x) container_of(x, struct ra_sd_priv, misc)

static inline void ra_sd_iow(struct ra_sd_priv *priv, off_t offset, u32 value)
{
	iowrite32(value, priv->regs + offset);
}

static inline u32 ra_sd_ior(struct ra_sd_priv *priv, off_t offset)
{
	return ioread32(priv->regs + offset);
}

static inline void ra_sd_read_rtcp_rx(struct ra_sd_priv *priv,
				      struct ra_sd_rtcp_rx_data_fpga *fpga)
{
	void __iomem *src = priv->regs + RA_SD_RTCP_RX_DATA;
	__ioread32_copy(fpga, src, sizeof(*fpga) / sizeof(u32));
}

static inline void ra_sd_read_rtcp_tx(struct ra_sd_priv *priv,
				      struct ra_sd_rtcp_tx_data_fpga *fpga)
{
	void __iomem *src = priv->regs + RA_SD_RTCP_TX_DATA;
	__ioread32_copy(fpga, src, sizeof(*fpga) / sizeof(u32));
}

int ra_sd_debugfs_init(struct ra_sd_priv *priv);

#endif /* RA_SD_MAIN_H */
