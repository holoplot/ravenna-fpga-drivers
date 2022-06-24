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

static ssize_t driver_version_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	return sysfs_emit(buf, "%s\n", ra_driver_version());
}
static DEVICE_ATTR_RO(driver_version);

static ssize_t ra_net_show_u32(struct device *dev,
			       struct device_attribute *attr,
			       char *buf, int reg)
{
	struct ra_net_priv *priv = netdev_priv(to_net_dev(dev));
	u32 v = ra_net_ior(priv, reg);

	return sysfs_emit(buf, "%d\n", v);
}


static ssize_t rtp_global_offset_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return ra_net_show_u32(dev, attr, buf, RA_NET_RTP_GLOBAL_OFFSET);
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

#define RA_NET_ATTR_U32_RO(_name,_reg)				\
static ssize_t _name##_show(struct device *dev,			\
			    struct device_attribute *attr,	\
			    char *buf)				\
{								\
	return ra_net_show_u32(dev, attr, buf, _reg);		\
}								\
static DEVICE_ATTR_RO(_name)

RA_NET_ATTR_U32_RO(udp_throttled_packets, RA_NET_PP_CNT_UDP_THROTTLE);

RA_NET_ATTR_U32_RO(rx_packets_parsed, RA_NET_PP_CNT_RX_PARSED);
RA_NET_ATTR_U32_RO(rx_queue_errors, RA_NET_PP_CNT_RX_QUEUE_ERR);
RA_NET_ATTR_U32_RO(rx_checksum_errors, RA_NET_PP_CNT_RX_IP_CHK_ERR);
RA_NET_ATTR_U32_RO(rx_stream_packets_dropped, RA_NET_PP_CNT_RX_STREAM_DROP);
RA_NET_ATTR_U32_RO(rx_stream_packets, RA_NET_PP_CNT_RX_STREAM);
RA_NET_ATTR_U32_RO(rx_legacy_packets, RA_NET_PP_CNT_RX_LEGACY);

RA_NET_ATTR_U32_RO(tx_stream_packets, RA_NET_PP_CNT_TX_STREAM);
RA_NET_ATTR_U32_RO(tx_legacy_packets, RA_NET_PP_CNT_TX_LEGACY);
RA_NET_ATTR_U32_RO(tx_stream_packets_lost, RA_NET_PP_CNT_TX_STREAM_LOST);

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
	&dev_attr_driver_version.attr,
	&dev_attr_rtp_global_offset.attr,
	&dev_attr_counter_reset.attr,
	&dev_attr_udp_throttled_packets.attr,
	&dev_attr_rx_packets_parsed.attr,
	&dev_attr_rx_queue_errors.attr,
	&dev_attr_rx_checksum_errors.attr,
	&dev_attr_rx_stream_packets_dropped.attr,
	&dev_attr_rx_stream_packets.attr,
	&dev_attr_rx_legacy_packets.attr,
	&dev_attr_tx_stream_packets.attr,
	&dev_attr_tx_legacy_packets.attr,
	&dev_attr_tx_stream_packets_lost.attr,
	&dev_attr_udp_filter_port.attr,
        NULL
};

const struct attribute_group ra_net_attr_group = {
        .name = "ra_net",
        .attrs = ra_net_attrs,
};
