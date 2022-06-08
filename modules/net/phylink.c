// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phylink.h>

#include "main.h"

static void ra_net_validate(struct phylink_config *config,
			    unsigned long *supported,
			    struct phylink_link_state *state)
{
}

static void ra_net_mac_config(struct phylink_config *config, unsigned int mode,
			      const struct phylink_link_state *state)
{
}

static void ra_net_mac_link_down(struct phylink_config *config,
				 unsigned int mode,
				 phy_interface_t interface)
{
}

static void ra_net_mac_link_up(struct phylink_config *config,
			       struct phy_device *phy,
			       unsigned int mode, phy_interface_t interface,
			       int speed, int duplex,
			       bool tx_pause, bool rx_pause)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct ra_net_priv *priv = netdev_priv(ndev);
	u32 v = RA_NET_AUTO_SPEED_MANUAL;

	printk(" ___ %s() speed %d, duplex %d\n", __func__, speed, duplex);

	switch (speed) {
	case SPEED_10:
		v |= RA_NET_AUTO_SPEED_10;
		break;
	case SPEED_100:
		v |= RA_NET_AUTO_SPEED_100;
		break;
	case SPEED_1000:
		v |= RA_NET_AUTO_SPEED_1000;
		break;
	default:
		dev_err(&ndev->dev, "Invalid speed setting");
		return;
	}

	ra_net_iow(priv, RA_NET_AUTO_SPEED_CTRL, v);
}

static const struct phylink_mac_ops ra_net_phylink_ops = {
	.validate	= ra_net_validate,
	.mac_config	= ra_net_mac_config,
	.mac_link_down	= ra_net_mac_link_down,
	.mac_link_up	= ra_net_mac_link_up,
};

static void ra_net_phylink_destroy(void *phylink)
{
	phylink_destroy(phylink);
}

int ra_net_phylink_init(struct ra_net_priv *priv)
{
	phy_interface_t phy_mode;

	phy_mode = PHY_INTERFACE_MODE_RGMII;
	of_get_phy_mode(priv->dev->of_node, &phy_mode);

	priv->phylink_config.dev = &priv->ndev->dev;
	priv->phylink_config.type = PHYLINK_NETDEV;
	priv->phylink_config.mac_capabilities = MAC_10 | MAC_100 | MAC_1000;

	__set_bit(PHY_INTERFACE_MODE_RGMII, priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_ID, priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_RXID, priv->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_TXID, priv->phylink_config.supported_interfaces);

	priv->phylink = phylink_create(&priv->phylink_config, priv->dev->fwnode,
				     phy_mode, &ra_net_phylink_ops);
	if (IS_ERR(priv->phylink))
		return PTR_ERR(priv->phylink);

	return devm_add_action_or_reset(priv->dev, ra_net_phylink_destroy, priv->phylink);
}
