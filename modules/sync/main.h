// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_SYNC_MAIN_H
#define RA_SYNC_MAIN_H

#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define RA_SYNC_N_EXT_SRC			10

#define RA_SYNC_IRQ_STAT0			0x00 /* R Interrupt Request 0 */
#define RA_SYNC_IRQ_STAT0_SD_EXT(n)		BIT(n)
#define RA_SYNC_IRQ_STAT0_SR_EXT(n)		BIT(10+n)
#define RA_SYNC_IRQ_STAT0_TYP_EXT(n)		BIT(20+n)
#define RA_SYNC_IRQ_STAT0_PLL_UNLOCK		BIT(30)
#define RA_SYNC_IRQ_STAT0_PHASE_ADJUST		BIT(31)

#define RA_SYNC_IRQ_STAT1			0x04 /* R Interrupt Request 1 */
#define RA_SYNC_IRQ_CTRL			0x08 /* R/W Interrupt Request Control */

#define RA_SYNC_SRV_KP_CTRL			0x0c /* R/W Servo prop factor control register */
#define RA_SYNC_SRV_KP_CTRL_MASK		0xffff

#define RA_SYNC_SRV_KI_CTRL			0x10 /* R/W Servo integrator factor control register */
#define RA_SYNC_SRV_KI_CTRL_MASK		0xffff

#define RA_SYNC_SRV_DEBUG			0x14 /* R Servo Debug register */

#define RA_SYNC_OUT0_CTRL			0x18 /* R/W Output control 0 */
#define RA_SYNC_OUT1_CTRL			0x1c /* R/W Output control 1 */
#define RA_SYNC_OUT_CTRL_WCLK			(0 << 9)
#define RA_SYNC_OUT_CTRL_DARS			(1 << 9)
#define RA_SYNC_OUT_CTRL_PTP_PPS		(2 << 9)
#define RA_SYNC_OUT_ENABLE			BIT(8)
#define RA_SYNC_OUT_PHASE_MASK			0xff

#define RA_SYNC_DARS_CS0			0x20 /* R/W AES3 output channel status data 0 */
#define RA_SYNC_DARS_CS1			0x24 /* R/W AES3 output channel status data 1 */
#define RA_SYNC_DARS_CS2			0x28 /* R/W AES3 output channel status data 2 */

#define RA_SYNC_MAIN_STAT			0x40 /* R Sync Main Status Register (a read deletes sticky bits) */
#define RA_SYNC_MAIN_STAT_PLL1_LOCKED		BIT(4)
#define RA_SYNC_MAIN_STAT_PHASE_ADJUST		BIT(3)
#define RA_SYNC_MAIN_STAT_PLL1_UNLOCK_S		BIT(0)

#define RA_SYNC_MAIN_CTRL			0x44 /* R/W Sync Main Control Register */
#define RA_SYNC_MAIN_WC_44_1			(0 << 13)
#define RA_SYNC_MAIN_WC_48			(1 << 13)
#define RA_SYNC_MAIN_WC_88_2			(2 << 13)
#define RA_SYNC_MAIN_WC_96			(3 << 13)
#define RA_SYNC_MAIN_WC_176_4			(4 << 13)
#define RA_SYNC_MAIN_WC_192			(5 << 13)
#define RA_SYNC_MAIN_GEN_EN			BIT(12)
#define RA_SYNC_MAIN_GEN_SOURCE_EXT(n)		(n << 8)
#define RA_SYNC_MAIN_SYS_44_1			(0 << 4)
#define RA_SYNC_MAIN_SYS_48			(1 << 4)
#define RA_SYNC_MAIN_SYS_88_2			(2 << 4)
#define RA_SYNC_MAIN_SYS_96			(3 << 4)
#define RA_SYNC_MAIN_SYS_176_4			(4 << 4)
#define RA_SYNC_MAIN_SYS_192			(5 << 4)
#define RA_SYNC_MAIN_SYNC_SRC_MASK		(0xf << 0)
#define RA_SYNC_MAIN_SYNC_SRC_EXT(n)		(n << 0)
#define RA_SYNC_MAIN_SYNC_SRC_PTP		(0xa << 0)
#define RA_SYNC_MAIN_SYNC_SRC_INTERNAL		(0xb << 0)
#define RA_SYNC_MAIN_SYNC_SRC_NONE		(0xc << 0)

#define RA_SYNC_EXT_SRC_COUNT			9
#define RA_SYNC_EXT_SRC_STAT(n)			(0x48 + (n*4)) /* R Status of external sync source 0 */

#define RA_SYNC_EXT_SRC_STAT_VID_FORMAT_SHIFT	12
#define RA_SYNC_EXT_SRC_STAT_VID_FORMAT_MASK	(0xf << RA_SYNC_EXT_VID_FORMAT_SHIFT)
#define RA_SYNC_EXT_SRC_STAT_SD_VID		BIT(10)
#define RA_SYNC_EXT_SRC_STAT_SD_AES3		BIT(9)
#define RA_SYNC_EXT_SRC_STAT_SD_WCLK		BIT(8)
#define RA_SYNC_EXT_SRC_STAT_FS_LOCK		BIT(4)
#define RA_SYNC_EXT_SRC_STAT_FS_MASK		0xf
#define RA_SYNC_EXT_SRC_STAT_FS_44_1		(0 << 0)
#define RA_SYNC_EXT_SRC_STAT_FS_48		(1 << 0)
#define RA_SYNC_EXT_SRC_STAT_FS_88_2		(2 << 0)
#define RA_SYNC_EXT_SRC_STAT_FS_96		(3 << 0)
#define RA_SYNC_EXT_SRC_STAT_FS_176_4		(4 << 0)
#define RA_SYNC_EXT_SRC_STAT_FS_192		(5 << 0)

#define RA_SYNC_EXT_SRC_CTRL(n)		(0x4c + (n*4)) /* R/W Control of external sync source 0 */

#define RA_SYNC_EXT_SRC_CTRL_PHASE_MASK	0xfff

struct ra_sync_priv {
	struct device		*dev;
	struct regmap		*regmap;
	struct miscdevice	misc;
	struct clk		*mclk;
	struct dentry		*debugfs;
	struct mutex		mutex;
};

#define to_ra_sync_priv(x) \
	container_of(x, struct ra_sync_priv, misc)

int ra_sync_debugfs_init(struct ra_sync_priv *priv);

#endif /* RA_SYNC_MAIN_H */
