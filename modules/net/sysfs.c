// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/device.h>
#include <linux/module.h>
#include <linux/netdevice.h>

#include <ravenna/version.h>
#include "main.h"

static ssize_t rav_core_version_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	u32 v;

	v = ra_net_ior(priv, RA_NET_RAV_CORE_VERSION);

	return sysfs_emit(buf, "%02X.%02X\n",
			  (v >> 8) & 0xff,
			  (v >> 0) & 0xff);
}
static DEVICE_ATTR_RO(rav_core_version);

static ssize_t rtp_global_offset_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	u32 v = ra_net_ior(priv, RA_NET_RTP_GLOBAL_OFFSET);

	return sysfs_emit(buf, "%u\n", v);
}

static ssize_t rtp_global_offset_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	int ret;
	u32 v;

	ret = kstrtou32(buf, 0, &v);
	if (ret < 0)
		return ret;

	ra_net_iow(priv, RA_NET_RTP_GLOBAL_OFFSET, v);

	return count;
}
static DEVICE_ATTR_RW(rtp_global_offset);

static ssize_t counter_reset_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	int ret;
	u32 v;

	ret = kstrtou32(buf, 0, &v);
	if (ret < 0)
		return ret;

	ra_net_iow(priv, RA_NET_PP_CNT_RST, v);

	return count;
}
static DEVICE_ATTR_WO(counter_reset);

static ssize_t udp_filter_port_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	u32 v = ra_net_ior(priv, RA_NET_PP_CNT_UDP_FILTER_CTRL);

	if (!(v & BIT(31)))
		v = 0;

	v &= 0xffff;

	return sysfs_emit(buf, "%d\n", v);
}

static ssize_t udp_filter_port_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	int ret;
	u32 v;

	ret = kstrtou32(buf, 0, &v);
	if (ret < 0)
		return ret;

	if (v > 0xffff)
		return -EINVAL;

	if (v > 0)
		v |= BIT(31);

	ra_net_iow(priv, RA_NET_PP_CNT_UDP_FILTER_CTRL, v);

	return count;
}
static DEVICE_ATTR_RW(udp_filter_port);

static struct attribute *ra_net_attrs[] = {
	&dev_attr_rav_core_version.attr,
	&dev_attr_rtp_global_offset.attr,
	&dev_attr_counter_reset.attr,
	&dev_attr_udp_filter_port.attr,
        NULL
};

const struct attribute_group ra_net_attr_group = {
        .name = "ra_net",
        .attrs = ra_net_attrs,
};
