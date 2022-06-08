// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_mdio.h>

#include "main.h"

#define RA_NET_MDIO_BUSY_TIMEOUT USEC_PER_SEC

static inline int ra_net_mdio_wait_ready(struct ra_net_priv *priv)
{
	u32 val;

	return read_poll_timeout(ra_net_ior, val,
				 !(val & RA_NET_MDIO_CTRL_BUSY),
				 1000, RA_NET_MDIO_BUSY_TIMEOUT, false,
				 priv, RA_NET_MDIO_CTRL);
}

static inline void ra_net_mdio_write_ctrl(struct ra_net_priv *priv,
					  int phy_id, int regnum, bool write)
{
	u32 val;

	val =  (phy_id << RA_NET_MDIO_CTRL_PHY_ADDR_SHIFT) & RA_NET_MDIO_CTRL_PHY_ADDR_MASK;
	val |= (regnum << RA_NET_MDIO_CTRL_ADDR_SHIFT) & RA_NET_MDIO_CTRL_ADDR_MASK;
	val |= RA_NET_MDIO_CTRL_BUSY;

	if (write)
		val |= RA_NET_MDIO_CTRL_WRITE;

	ra_net_iow(priv, RA_NET_MDIO_CTRL, val);
}

static int ra_net_mdio_read(struct mii_bus *mii, int phy_id, int regnum)
{
	struct ra_net_priv *priv = mii->priv;
	int ret;

	ret = ra_net_mdio_wait_ready(priv);
	if (ret < 0)
		return ret;

	spin_lock(&priv->mdio_lock);

	ra_net_mdio_write_ctrl(priv, phy_id, regnum, false);

	ret = ra_net_mdio_wait_ready(priv);
	if (ret == 0)
		ret = ra_net_ior(priv, RA_NET_MDIO_DATA);

	spin_unlock(&priv->mdio_lock);

	return ret;
}

static int ra_net_mdio_write(struct mii_bus *mii, int phy_id,
			     int regnum, u16 data)
{
	struct ra_net_priv *priv = mii->priv;
	int ret;

	ret = ra_net_mdio_wait_ready(priv);
	if (ret < 0)
		return ret;

	spin_lock(&priv->mdio_lock);

	ra_net_iow(priv, RA_NET_MDIO_DATA, data);
	ra_net_mdio_write_ctrl(priv, phy_id, regnum, true);

	ret = ra_net_mdio_wait_ready(priv);

	spin_unlock(&priv->mdio_lock);

	return ret;
}

int ra_net_mdio_init(struct ra_net_priv *priv)
{
	struct device_node *mdio_node;
	struct mii_bus *mii;
	int ret;

	mdio_node = of_get_child_by_name(priv->dev->of_node, "mdio");
	if (!mdio_node)
		return 0;

	if (!of_device_is_available(mdio_node)) {
		ret = -ENODEV;
		goto out_put_node;
	}

	mii = devm_mdiobus_alloc(priv->dev);
	if (!mii) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	snprintf(mii->id, MII_BUS_ID_SIZE, "%s", dev_name(priv->dev));
	mii->name = "ravenna-net-mdio";
	mii->parent = priv->dev;
	mii->read = ra_net_mdio_read;
	mii->write = ra_net_mdio_write;
	mii->priv = priv;

	ret = devm_of_mdiobus_register(priv->dev, mii, mdio_node);

out_put_node:
	of_node_put(mdio_node);

	return ret;
}
