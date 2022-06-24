// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/ethtool.h>
#include <linux/phylink.h>
#include <linux/of_platform.h>
#include <uapi/linux/net_tstamp.h>

#include <ravenna/version.h>
#include "main.h"

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

static int ra_net_ethtool_get_ts_info(struct net_device *ndev,
				      struct ethtool_ts_info *info)
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

static int ra_net_set_link_ksettings(struct net_device *ndev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	return phylink_ethtool_ksettings_set(priv->phylink, cmd);
}

static int ra_net_get_link_ksettings(struct net_device *ndev,
				     struct ethtool_link_ksettings *cmd)
{
	struct ra_net_priv *priv = netdev_priv(ndev);

	return phylink_ethtool_ksettings_get(priv->phylink, cmd);
}

const struct ethtool_ops ra_net_ethtool_ops = {
	.get_drvinfo		= ra_net_ethtool_getdrvinfo,
	.get_msglevel		= ra_net_ethtool_getmsglevel,
	.set_msglevel		= ra_net_ethtool_setmsglevel,
	.get_link		= ethtool_op_get_link,
	.get_regs_len		= ra_net_ethtool_getregslen,
	.get_regs		= ra_net_ethtool_getregs,
	.get_ts_info		= ra_net_ethtool_get_ts_info,
	.get_module_info	= ra_net_ethtool_get_module_info,
	.get_module_eeprom	= ra_net_ethtool_get_module_eeprom,
	.set_link_ksettings	= ra_net_set_link_ksettings,
	.get_link_ksettings	= ra_net_get_link_ksettings,
};
