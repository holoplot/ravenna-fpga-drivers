// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/ethtool.h>
#include <linux/phylink.h>
#include <linux/of_platform.h>
#include <linux/version.h>

#include <uapi/linux/net_tstamp.h>

#include <ravenna/version.h>

#include "main.h"

static const char ra_net_gstrings_stats[][ETH_GSTRING_LEN] = {
	"udp_throttled_packets",
	"fifo_err_cnt",

	"rx_packets_parsed",
	"rx_queue_errors",
	"rx_checksum_errors",
	"rx_stream_packets_dropped",
	"rx_stream_packets",
	"rx_legacy_packets",
	"rx_unicast_packets",
	"rx_broadcast_packets",
	"rx_dropped_frames",
	"rx_fcs_errors",

	"tx_stream_packets",
	"tx_legacy_packets",
	"tx_stream_packets_lost",
	"tx_unicast_packets",
	"tx_multicast_packets",
	"tx_broadcast_packets",
	"tx_pad_packets",
	"tx_oversize_packets",
};

struct ra_net_stats {
	u64 udp_throttled_packets;
	u64 fifo_err_cnt;

	u64 rx_packets_parsed;
	u64 rx_queue_errors;
	u64 rx_checksum_errors;
	u64 rx_stream_packets_dropped;
	u64 rx_stream_packets;
	u64 rx_legacy_packets;
	u64 rx_unicast_packets;
	u64 rx_broadcast_packets;
	u64 rx_dropped_frames;
	u64 rx_fcs_errors;

	u64 tx_stream_packets;
	u64 tx_legacy_packets;
	u64 tx_stream_packets_lost;
	u64 tx_unicast_packets;
	u64 tx_multicast_packets;
	u64 tx_broadcast_packets;
	u64 tx_pad_packets;
	u64 tx_oversize_packets;
};

static void ra_net_read_stats(struct ra_net_priv *priv,
			      struct ra_net_stats *stats)
{
	BUILD_BUG_ON(ARRAY_SIZE(ra_net_gstrings_stats) != sizeof(*stats) / sizeof(u64));

	stats->udp_throttled_packets =
		ra_net_ior(priv, RA_NET_PP_CNT_UDP_THROTTLE);
	stats->fifo_err_cnt =
		ra_net_ior(priv, RA_NET_FIFO_ERR_CNT);

	stats->rx_packets_parsed =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_PARSED);
	stats->rx_queue_errors =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_QUEUE_ERR);
	stats->rx_checksum_errors =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_IP_CHK_ERR);
	stats->rx_stream_packets_dropped =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_STREAM_DROP);
	stats->rx_stream_packets =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_STREAM);
	stats->rx_legacy_packets =
		ra_net_ior(priv, RA_NET_PP_CNT_RX_LEGACY);
	stats->rx_unicast_packets =
		ra_net_ior(priv, RA_NET_RX_UNICAST_PKT_CNT);
	stats->rx_broadcast_packets =
		ra_net_ior(priv, RA_NET_RX_BROADCAST_PKT_CNT);
	stats->rx_dropped_frames =
		ra_net_ior(priv, RA_NET_RX_DROPPED_FRAMES_CNT);
	stats->rx_fcs_errors =
		ra_net_ior(priv, RA_NET_RX_FCS_ERR_CNT);

	stats->tx_stream_packets =
		ra_net_ior(priv, RA_NET_PP_CNT_TX_STREAM);
	stats->tx_legacy_packets =
		ra_net_ior(priv, RA_NET_PP_CNT_TX_LEGACY);
	stats->tx_stream_packets_lost =
		ra_net_ior(priv, RA_NET_PP_CNT_TX_STREAM_LOST);
	stats->tx_unicast_packets =
		ra_net_ior(priv, RA_NET_TX_UNICAST_PKT_CNT);
	stats->tx_multicast_packets =
		ra_net_ior(priv, RA_NET_TX_MULTICAST_PKT_CNT);
	stats->tx_broadcast_packets =
		ra_net_ior(priv, RA_NET_TX_BROADCAST_PKT_CNT);
	stats->tx_pad_packets =
		ra_net_ior(priv, RA_NET_TX_PAD_PKT_CNT);
	stats->tx_oversize_packets =
		ra_net_ior(priv, RA_NET_TX_OVERSIZE_PKT_CNT);
}

static void ra_net_get_strings(struct net_device *netdev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, &ra_net_gstrings_stats, sizeof(ra_net_gstrings_stats));
		break;
	default:
		WARN_ON(1);
		break;
	}
}

static int ra_net_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(ra_net_gstrings_stats);
	default:
		return -EINVAL;
	}
}

static void ra_net_get_ethtool_stats(struct net_device *ndev,
				  struct ethtool_stats *estats, u64 *data)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	struct ra_net_stats stats;

	ra_net_read_stats(priv, &stats);

	memcpy(data, &stats, sizeof(stats));
}


static void ra_net_ethtool_getdrvinfo(struct net_device *ndev,
				      struct ethtool_drvinfo *info)
{
	struct ra_net_priv *priv = netdev_priv(ndev);
	const char *version = ra_driver_version();

	strlcpy(info->driver, priv->dev->driver->name, sizeof(info->driver));
	strlcpy(info->version, version, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(priv->dev), sizeof(info->bus_info));
}

static u32 ra_net_ethtool_getmsglevel(struct net_device *ndev)
{
	return NETIF_MSG_LINK;
}

static void ra_net_ethtool_setmsglevel(struct net_device *ndev, u32 level)
{
}

static int ra_net_ethtool_getregslen(struct net_device *ndev)
{
	return 0;
}

static void ra_net_ethtool_getregs(struct net_device *ndev,
				   struct ethtool_regs* regs,
				   void *buf)
{
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static int ra_net_ethtool_get_ts_info(struct net_device *ndev,
				      struct kernel_ethtool_ts_info *info)
#else
static int ra_net_ethtool_get_ts_info(struct net_device *ndev,
				      struct ethtool_ts_info *info)
#endif
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	dev_dbg(priv->dev, "%s()\n", __func__);

	info->phc_index = priv->phc_index;
	info->so_timestamping =
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;
	info->tx_types =
		BIT(HWTSTAMP_TX_OFF) |
		BIT(HWTSTAMP_TX_ON);
	info->rx_filters =
		BIT(HWTSTAMP_FILTER_NONE) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_SYNC) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ);

	return 0;
}

static int ra_net_ethtool_get_module_info(struct net_device *ndev,
					  struct ethtool_modinfo *modinfo)
{
	return -EOPNOTSUPP;
}

static int ra_net_ethtool_get_module_eeprom(struct net_device *ndev,
					    struct ethtool_eeprom *ee, u8 *data)
{
	return -EOPNOTSUPP;
}

static int
ra_net_ethtool_get_link_ksettings(struct net_device *ndev,
				  struct ethtool_link_ksettings *cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(priv->phylink, cmd);
}

static int
ra_net_ethtool_set_link_ksettings(struct net_device *ndev,
				  const struct ethtool_link_ksettings *cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	return phylink_ethtool_ksettings_set(priv->phylink, cmd);
}

const struct ethtool_ops ra_net_ethtool_ops = {
	.get_drvinfo		= ra_net_ethtool_getdrvinfo,
	.get_strings		= ra_net_get_strings,
	.get_sset_count		= ra_net_get_sset_count,
	.get_ethtool_stats	= ra_net_get_ethtool_stats,
	.get_msglevel		= ra_net_ethtool_getmsglevel,
	.set_msglevel		= ra_net_ethtool_setmsglevel,
	.get_link		= ethtool_op_get_link,
	.get_regs_len		= ra_net_ethtool_getregslen,
	.get_regs		= ra_net_ethtool_getregs,
	.get_ts_info		= ra_net_ethtool_get_ts_info,
	.get_module_info	= ra_net_ethtool_get_module_info,
	.get_module_eeprom	= ra_net_ethtool_get_module_eeprom,
	.get_link_ksettings	= ra_net_ethtool_get_link_ksettings,
	.set_link_ksettings	= ra_net_ethtool_set_link_ksettings,
};
