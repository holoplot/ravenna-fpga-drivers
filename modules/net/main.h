#ifndef RAVENNA_NET_MAIN_H
#define RAVENNA_NET_MAIN_H

#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/phylink.h>
#include <linux/ptp_classify.h>
#include <linux/dmaengine.h>

#include "regs.h"

#define RA_NET_TX_SKB_LIST_SIZE	64
#define RA_NET_TX_TS_LIST_SIZE	64

/* raw timestamp data read from FPGA */
struct ptp_packet_fpga_timestamp
{
#ifdef __LITTLE_ENDIAN
	u16 seconds_hi;
	u16 start_of_ts;
	u32 seconds;
	u32 nanoseconds;
	u16 sequence_id;
	u16 reserved;
#else /* __ BIG_ENDIAN */
#error Big Endian platforms are unsupported
#endif
} __packed;

#define RA_NET_TX_TIMESTAMP_START_OF_TS	0x1588

struct ra_net_tx_ts {
	bool enable;
	unsigned int reenable_irq;
	struct work_struct work;
	spinlock_t lock;

	struct sk_buff *skb_ptr[RA_NET_TX_SKB_LIST_SIZE];
	unsigned int skb_rd_idx;
	unsigned int skb_wr_idx;

	struct ptp_packet_fpga_timestamp fpga_ts[RA_NET_TX_TS_LIST_SIZE];
	unsigned int ts_rd_idx;
	unsigned int ts_wr_idx;
};

struct ra_net_priv {
	void __iomem *regs;

	spinlock_t lock;
	spinlock_t reg_lock;
	spinlock_t mdio_lock;

	struct device	 	*dev;
	struct net_device 	*ndev;
	struct napi_struct	napi;

	struct phylink		*phylink;
	struct phylink_config	phylink_config;

	struct dma_chan		*dma_rx_chan;
	dma_addr_t		dma_addr;

	bool tx_throttle;

	int phc_index;

	struct ra_net_tx_ts tx_ts;
	bool rx_ts_enable;

	int rx_dropped_packets_at_probe;
};

static inline void ra_net_iow(struct ra_net_priv *priv, off_t offset, u32 value)
{
	iowrite32(value, priv->regs + offset);
}

static inline void ra_net_iow_rep(struct ra_net_priv *priv, off_t offset,
				  const void *buf, size_t len)
{
	iowrite32_rep(priv->regs + offset, buf, len / sizeof(u32));
}

static inline u32 ra_net_ior(struct ra_net_priv *priv, off_t offset)
{
	return ioread32(priv->regs + offset);
}

static inline void ra_net_ior_rep(struct ra_net_priv *priv, off_t offset,
				  void *buf, size_t len)
{
	ioread32_rep(priv->regs + offset, buf, len  / sizeof(u32));
}

static inline void ra_net_iow_mask(struct ra_net_priv *priv, off_t offset,
				   u32 mask, u32 val)
{
	u32 r;
	unsigned long flags;

	spin_lock_irqsave(&priv->reg_lock, flags);

	r = ra_net_ior(priv, offset);
	r &= ~mask;
	r |= val;
	ra_net_iow(priv, offset, r);

	spin_unlock_irqrestore(&priv->reg_lock, flags);
}

static inline void ra_net_irq_enable(struct ra_net_priv *priv, u32 bit)
{
	ra_net_iow_mask(priv, RA_NET_IRQ_DISABLE, bit, 0);
}

static inline void ra_net_irq_disable(struct ra_net_priv *priv, u32 bit)
{
	ra_net_iow_mask(priv, RA_NET_IRQ_DISABLE, bit, bit);
}

static inline void ra_net_pp_irq_enable(struct ra_net_priv *priv, u32 bit)
{
	ra_net_iow_mask(priv, RA_NET_PP_IRQ_DISABLE, bit, 0);
}

static inline void ra_net_pp_irq_disable(struct ra_net_priv *priv, u32 bit)
{
	ra_net_iow_mask(priv, RA_NET_PP_IRQ_DISABLE, bit, bit);
}

extern const struct ethtool_ops ra_net_ethtool_ops;
extern const struct attribute_group ra_net_attr_group;

int ra_net_phylink_init(struct ra_net_priv *priv);
int ra_net_mdio_init(struct ra_net_priv *priv);

void ra_net_tx_ts_irq(struct ra_net_priv *priv);
void ra_net_flush_tx_ts(struct ra_net_priv *priv);
bool ra_net_tx_ts_queue(struct ra_net_priv *priv, struct sk_buff *skb);
void ra_net_tx_ts_init(struct ra_net_priv *priv);
int ra_net_hwtstamp_ioctl(struct net_device *ndev,
			  struct ifreq *ifr, int cmd);
void ra_net_rx_read_timestamp(struct ra_net_priv *priv, struct sk_buff *skb);
void ra_net_rx_apply_timestamp(struct ra_net_priv *priv, struct sk_buff *skb,
			       struct ptp_packet_fpga_timestamp *ts);

int ra_net_dma_probe(struct ra_net_priv *priv);
void ra_net_dma_flush(struct ra_net_priv *priv);
void ra_net_dma_rx(struct ra_net_priv *priv);

#endif /* RAVENNA_NET_MAIN_H */
