// SPDX-License-Identifier: GPL-2.0-or-later

// #define DEBUG 1

#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/pps_kernel.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/clk.h>

#include "regs.h"

#define RA_EVENT_OUT_MAX_PERIOD		(1 * NSEC_PER_SEC)
#define RA_PTP_ADJ_TIME_MAX_OFFSET	(1 * NSEC_PER_SEC)

struct ra_ptp_priv {
	struct device		*dev;
	void __iomem		*regs;
	struct ptp_clock	*ptp_clock;
	struct ptp_clock_info	ptp_clock_info;
	u64			last_ptp_timestamp;
	u32			last_rtp_timestamp;
	spinlock_t		lock;
};

static u32 ra_ptp_ior(struct ra_ptp_priv *priv, off_t reg)
{
	return ioread32(priv->regs + reg);
}

static void ra_ptp_ior_rep(struct ra_ptp_priv *priv, off_t reg,
			   void *dst, size_t len)
{
	ioread32_rep(priv->regs + reg, dst, len / sizeof(u32));
}

static void ra_ptp_iow(struct ra_ptp_priv *priv, off_t reg, u32 val)
{
	iowrite32(val, priv->regs + reg);
}

static void ra_ptp_write_mask(struct ra_ptp_priv *priv,
			      off_t reg, u32 mask, u32 val)
{
	unsigned long flags;
	u32 v;

	spin_lock_irqsave(&priv->lock, flags);

	v = ra_ptp_ior(priv, reg);
	v &= ~mask;
	v |= mask & val;
	ra_ptp_iow(priv, reg, v);

	spin_unlock_irqrestore(&priv->lock, flags);
}

static void ra_ptp_cmd(struct ra_ptp_priv *priv, u32 cmd)
{
	ra_ptp_iow(priv, RA_PTP_CMD, cmd);
}

static int ra_ptp_set_per_out(struct ra_ptp_priv *priv, int ns)
{
	struct device *dev = priv->dev;
	unsigned long flags;

	if (ns > RA_EVENT_OUT_MAX_PERIOD) {
		dev_err(dev, "Invalid interval for periodic output: %d\n", ns);
		return -EINVAL;
	}

	spin_lock_irqsave(&priv->lock, flags);

	ra_ptp_iow(priv, RA_PTP_EVENT_OUT_MODE, 0);
	ra_ptp_iow(priv, RA_PTP_EVENT_OUT_NS_INTERVAL, ns);

	if (ns > 0) {
		ra_ptp_iow(priv, RA_PTP_EVENT_OUT_MODE,
			   RA_PTP_EVENT_OUT_MODE_PERIODIC |
			   RA_PTP_EVENT_OUT_MODE_ENABLE);

		dev_info(dev, "Periodic output activated with interval of %d ns\n", ns);
	} else {
		dev_info(dev, "Periodic output deactivated\n");
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

/*
 * PTP clock operations
 */

#define to_ra_ptp_priv(ptp) \
	container_of(ptp, struct ra_ptp_priv, ptp_clock_info)

static int ra_ptp_adjfreq(struct ptp_clock_info *ptp, int ppb)
{
	struct ra_ptp_priv *priv = to_ra_ptp_priv(ptp);
	unsigned long flags;
	u32 val = 0;

	if (ppb == 0)
		return 0;

	if (ppb < 0) {
		ppb = -ppb;
		val |= RA_PTP_DRIFT_CORRECTION_NEGATIVE;
	}

	if (ppb > RA_PTP_DRIFT_CORRECTION_MAX_PPB) {
		dev_info(priv->dev,
			 "PTP hw clock adjust: requested ppb %d beyond "
			 "max. drift correction %i => limiting\n",
			 ppb, RA_PTP_DRIFT_CORRECTION_MAX_PPB);
		ppb = RA_PTP_DRIFT_CORRECTION_MAX_PPB;
	}

	val |= ppb & RA_PTP_DRIFT_CORRECTION_PPB_VALUE_MASK;

	spin_lock_irqsave(&priv->lock, flags);
	ra_ptp_iow(priv, RA_PTP_DRIFT_CORRECTION, val);
	ra_ptp_cmd(priv, RA_PTP_CMD_APPLY_DRIFT_CORRECTION);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int ra_ptp_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ra_ptp_priv *priv = to_ra_ptp_priv(ptp);
	unsigned long flags;
	int ret;
	u32 val;

	spin_lock_irqsave(&priv->lock, flags);

	ra_ptp_cmd(priv, RA_PTP_CMD_READ_CLOCK);

	ret = read_poll_timeout_atomic(ra_ptp_ior, val,
				       val & RA_PTP_STATUS_READ_CLOCK_VALID,
				       1, 100, 0, priv, RA_PTP_STATUS);
	if (ret == 0) {
		ts->tv_sec = ra_ptp_ior(priv, RA_PTP_READ_TIME_SECONDS_H);
		ts->tv_sec <<= 32ULL;
		ts->tv_sec |= ra_ptp_ior(priv, RA_PTP_READ_TIME_SECONDS);
		ts->tv_nsec = ra_ptp_ior(priv, RA_PTP_READ_TIME_NANOSECONDS);
	} else {
		dev_err(priv->dev, "Timeout waiting for clock validity\n");
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	dev_dbg(priv->dev, "%s() tv_sec %lld tv_nsec %ld, ret %d\n",
		__func__, ts->tv_sec, ts->tv_nsec, ret);

	return ret;
}

static int ra_ptp_settime(struct ptp_clock_info *ptp,
			  const struct timespec64 *ts)
{
	struct ra_ptp_priv *priv = to_ra_ptp_priv(ptp);
	unsigned long flags;

	dev_dbg(priv->dev, "%s() tv_sec %lld tv_nsec %ld\n",
		__func__, ts->tv_sec, ts->tv_nsec);

	spin_lock_irqsave(&priv->lock, flags);
	ra_ptp_iow(priv, RA_PTP_SET_TIME_SECONDS_H, ts->tv_sec >> 32ULL);
	ra_ptp_iow(priv, RA_PTP_SET_TIME_SECONDS, ts->tv_sec);
	ra_ptp_iow(priv, RA_PTP_SET_TIME_NANOSECONDS, ts->tv_nsec);
	ra_ptp_cmd(priv, RA_PTP_CMD_WRITE_CLOCK);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int ra_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ra_ptp_priv *priv = to_ra_ptp_priv(ptp);
	struct device *dev = priv->dev;
	struct timespec64 ts;
	unsigned long flags;
	int sign = 1, ret, res;
	u32 val = 0;

	dev_dbg(dev, "%s() delta %lld\n", __func__, delta);

	if (delta == 0)
		return 0;

	if (delta < 0) {
		delta *= -1;
		sign = -1;
		val |= RA_PTP_OFFSET_CORRECTION_NEGATIVE;
	}

	if (delta <= RA_PTP_ADJ_TIME_MAX_OFFSET) {
		val |= delta;

		spin_lock_irqsave(&priv->lock, flags);
		ra_ptp_iow(priv, RA_PTP_OFFSET_CORRECTION, val);
		ra_ptp_cmd(priv, RA_PTP_CMD_APPLY_CLOCK_OFFSET);
		spin_unlock_irqrestore(&priv->lock, flags);

		dev_dbg(dev, "%s(): PTP hw clock adjust: %c%lld ns (0x%02x)\n",
			__func__, (sign > 0) ? '+': '-', delta, val);

		return 0;
	}

	dev_info(dev, "PTP hw clock adjust: max. offset exceeded, using settime\n");

	ret = ra_ptp_gettime(ptp, &ts);
	if (ret < 0) {
		dev_err(dev, "%s(): PTP clock gettime failed: %d",
			__func__, ret);
		return ret;
	}

	res = div_u64(delta, NSEC_PER_SEC);
	ts.tv_sec  += sign * res;
	ts.tv_nsec += sign * (delta - (res * NSEC_PER_SEC));

	if (ts.tv_nsec < 0) {
		ts.tv_sec--;
		ts.tv_nsec += NSEC_PER_SEC;
	}

	ret = ra_ptp_settime(ptp, &ts);
	if (ret < 0) {
		dev_err(dev, "%s(): PTP clock settime failed: %d",
			__func__, ret);
		return ret;
	}

	return 0;
}

static int ra_ptp_enable(struct ptp_clock_info *ptp,
			 struct ptp_clock_request *rq,
			 int on)
{
	struct ra_ptp_priv *priv = to_ra_ptp_priv(ptp);
	struct device *dev = priv->dev;
	unsigned int ns = 0;

	dev_dbg(dev, "%s()\n", __func__);

	switch(rq->type) {
	case PTP_CLK_REQ_EXTTS:
		if (rq->extts.index != 0) {
			dev_err(dev, "%s(): invalid index %d for EXTTS\n",
				__func__, rq->extts.index);
			return -EINVAL;
		}

		ra_ptp_write_mask(priv, RA_PTP_IRQ_DISABLE,
				  RA_PTP_IRQ_EXTTS,
				  on ? 0 : RA_PTP_IRQ_EXTTS);
		ra_ptp_write_mask(priv, RA_PTP_EXTTS_MODE,
				  RA_PTP_EXTTS_MODE_ENABLE_EXTTS,
				  on ? RA_PTP_EXTTS_MODE_ENABLE_EXTTS : 0);

		dev_dbg(dev, "%s: %sable EXTTS\n", __func__, on ? "en" : "dis");

		return 0;

	case PTP_CLK_REQ_PEROUT:
		if (rq->perout.index != 0) {
			dev_err(dev, "%s(): invalid index %d for PEROUT\n",
				__func__, rq->perout.index);
			return -EINVAL;
		}

		if (on)
			ns = rq->perout.period.sec * NSEC_PER_SEC +
			     rq->perout.period.nsec;

		return ra_ptp_set_per_out(priv, ns);

	case PTP_CLK_REQ_PPS:
		ra_ptp_write_mask(priv, RA_PTP_IRQ_DISABLE,
					RA_PTP_IRQ_PPS,
					on ? 0 : RA_PTP_IRQ_PPS);

		dev_dbg(dev, "%s(): %sable PPS\n", __func__, on ? "en" : "dis");
		return 0;

	default:
		dev_err(dev, "%s(): requested type %d not supported\n",
			__func__, rq->type);
		break;
	}

	return -EOPNOTSUPP;
}

/* Interrupt handlers */

static void ra_ptp_extts_irq(struct ra_ptp_priv *priv)
{
	struct device *dev = priv->dev;
	u32 ts_cnt;

	spin_lock(&priv->lock);

	ts_cnt = ra_ptp_ior(priv, RA_PTP_EXTTS_TS_CNT);
	dev_dbg(dev, "%s() ts_cnt %d\n", __func__, ts_cnt);

	if (ts_cnt >= RA_PTP_EXTTS_MAX_TS_CNT) {
		u32 status = ra_ptp_ior(priv, RA_PTP_STATUS);
		if (status & RA_PTP_STATUS_EXTTS_FIFO_OVFLW) {
			dev_err(dev, "PTP hw clock: event timestamp FIFO overflow!"
				" => Event timestamp(s) may be lost or damaged\n");
			ra_ptp_cmd(priv, RA_PTP_CMD_RESET_EXTTS_FIFO_OVFLW);
		}
	}

	while (ts_cnt--) {
		struct ra_ptp_extts_fpga_timestamp extts;
		struct ptp_clock_event event = {
			.type = PTP_CLOCK_EXTTS,
		};
		u32 sot, ctr;
		u64 seconds;

		ctr = RA_PTP_EXTTS_TIMESTAMP_WORDLEN;

		do {
			sot = ra_ptp_ior(priv, RA_PTP_EXTTS_DATA);
			dev_dbg(dev, "%s(): sot: 0x%04X\n", __func__, sot);
		} while (((sot >> 16) != RA_PTP_EXTTS_START_OF_TS) && --ctr);

		if (ctr) {
			extts.seconds_hi = sot & 0xffff;
			ra_ptp_ior_rep(priv, RA_PTP_EXTTS_DATA, &extts.seconds,
				       sizeof(extts) - sizeof(u32));

			seconds =  (u64)extts.seconds_hi << 32ULL;
			seconds += (u64)extts.seconds;

			event.timestamp =  seconds * NSEC_PER_SEC;
			event.timestamp += (u64)extts.nanoseconds;

			priv->last_ptp_timestamp = event.timestamp;
			priv->last_rtp_timestamp = extts.rtp_ts;

			dev_dbg(dev, "%s(): event TS %lld\n",
				__func__, event.timestamp);

			ptp_clock_event(priv->ptp_clock, &event);
		} else {
			dev_dbg(dev, "%s: no start of timestamp found\n",
				__func__);
		}
	}

	spin_unlock(&priv->lock);
}

static void ra_ptp_pps_irq(struct ra_ptp_priv *priv)
{
	struct ptp_clock_event event;
	struct timespec64 ts;
	int ret;

	dev_dbg(priv->dev, "%s()\n", __func__);

	ra_ptp_cmd(priv, RA_PTP_CMD_ACK_PPS_IRQ);

	/*
	* Assuming that the PPS IRQ is directly related to the start
	* of a second:
	* read the ptp clock and use only the seconds part to provide
	* the exact time at the rising edge of the PPS pulse
	*/

	ret = ra_ptp_gettime(&priv->ptp_clock_info, &ts);
	if (ret < 0) {
		dev_err(priv->dev, "%s(): ra_ptp_gettime() failed: %d\n",
			__func__, ret);
		return;
	}

	event.type = PTP_CLOCK_PPSUSR;
	event.pps_times.ts_real.tv_sec = ts.tv_sec;
	event.pps_times.ts_real.tv_nsec	= 0;

	ptp_clock_event(priv->ptp_clock, &event);
}

static irqreturn_t ra_ptp_irqhandler(int irq, void *devid)
{
	struct ra_ptp_priv *priv = devid;
	irqreturn_t ret = IRQ_NONE;

	dev_dbg(priv->dev, "%s()\n", __func__);

	for (;;) {
		u32 irqs = ra_ptp_ior(priv, RA_PTP_IRQS);
		u32 mask = ra_ptp_ior(priv, RA_PTP_IRQ_DISABLE);

		irqs &= ~mask;

		if (!irqs)
			break;

		if (irqs & RA_PTP_IRQ_EXTTS) {
			ra_ptp_extts_irq(priv);
			ret = IRQ_HANDLED;
		}

		if (irqs & RA_PTP_IRQ_PPS) {
			ra_ptp_pps_irq(priv);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

/* sysfs */

static ssize_t rtp_timestamp_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct ra_ptp_priv *priv = dev->platform_data;
	u64 last_ptp_timestamp;
	u32 last_rtp_timestamp;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	last_ptp_timestamp = priv->last_ptp_timestamp;
	last_rtp_timestamp = priv->last_rtp_timestamp;
	spin_unlock_irqrestore(&priv->lock, flags);

	return sysfs_emit(buf, "%llu %u\n",
			  last_ptp_timestamp, last_rtp_timestamp);
}
static DEVICE_ATTR_RO(rtp_timestamp);

static struct attribute *ra_ptp_attrs[] = {
	&dev_attr_rtp_timestamp.attr,
	NULL
};

static const struct attribute_group ra_ptp_attr_group = {
	.name = "ravenna_ptp",
	.attrs = ra_ptp_attrs,
};
ATTRIBUTE_GROUPS(ra_ptp);

/* platform */

static void ra_ptp_ptp_unregister(void *data) {
	ptp_clock_unregister((struct ptp_clock *) data);
}

static int ra_ptp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	u32 id, per_out_interval = 0;
	struct ra_ptp_priv *priv;
	struct resource *res;
	int ret, irq;

	dev_dbg(dev, "%s", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	dev_set_drvdata(dev, priv);
	dev->platform_data = priv;
	priv->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	id = ra_ptp_ior(priv, RA_PTP_ID);
	if ((id & RA_PTP_ID_MASK) != RA_PTP_ID_VALUE) {
		dev_err(dev, "Unexpected ID value: %02x\n", id);
		return -ENODEV;
	}

	irq = of_irq_get(node, 0);
	ret = devm_request_irq(dev, irq, ra_ptp_irqhandler, IRQF_SHARED,
			       dev_name(dev), priv);
	if (ret < 0) {
		dev_err(dev, "could not map Ravenna sync IRQ\n");
		return ret;
	}

	ret = devm_device_add_groups(dev, ra_ptp_groups);
	if (ret < 0)
		return ret;

	priv->ptp_clock_info.max_adj	= RA_PTP_DRIFT_CORRECTION_MAX_PPB;
	priv->ptp_clock_info.n_ext_ts	= RA_PTP_EXTTS_CNT;
	priv->ptp_clock_info.n_per_out	= RA_PTP_PEROUT_CNT;
	priv->ptp_clock_info.adjfreq	= ra_ptp_adjfreq;
	priv->ptp_clock_info.adjtime	= ra_ptp_adjtime;
	priv->ptp_clock_info.gettime64	= ra_ptp_gettime;
	priv->ptp_clock_info.settime64	= ra_ptp_settime;
	priv->ptp_clock_info.enable	= ra_ptp_enable;
	priv->ptp_clock_info.owner	= THIS_MODULE;
	strlcpy(priv->ptp_clock_info.name, "ravenna_ptp",
		sizeof(priv->ptp_clock_info.name)-1);

	if (id & RA_PTP_ID_PPS_AVAILABLE)
		priv->ptp_clock_info.pps = 1;
	else
		dev_info(dev, "Device does not support PPS\n");

	priv->ptp_clock = ptp_clock_register(&priv->ptp_clock_info, dev);
	if (IS_ERR(priv->ptp_clock))
		return PTR_ERR(priv->ptp_clock);

	ret = devm_add_action_or_reset(dev, ra_ptp_ptp_unregister,
				       priv->ptp_clock);
	if (ret < 0)
		return ret;

	/* The ethernet driver will access the PTP clock through the driver-data */
	platform_set_drvdata(pdev, priv->ptp_clock);

	dev_info(dev, "Ravenna PTP, clock index %d\n",
		 ptp_clock_index(priv->ptp_clock));

	of_property_read_u32(node, "lawo,periodic-output-interval-ns",
			     &per_out_interval);
	ra_ptp_set_per_out(priv, per_out_interval);

	return 0;
}

static const struct of_device_id ra_ptp_of_ids[] = {
	{ .compatible = "lawo,ravenna-ptp" },
	{}
};
MODULE_DEVICE_TABLE(of, ra_ptp_of_ids);

static struct platform_driver ra_ptp_driver =
{
	.probe = ra_ptp_probe,
	.driver = {
		.name = "ravenna_ptp",
		.of_match_table = ra_ptp_of_ids,
	},
};

module_platform_driver(ra_ptp_driver);

MODULE_DESCRIPTION("Ravenna PTP driver");
MODULE_LICENSE("GPL");
