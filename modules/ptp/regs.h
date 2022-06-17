// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef RA_PTP_REGS
#define RA_PTP_REGS

// external event timestamp data read from FPGA
struct ra_ptp_extts_fpga_timestamp {
#ifdef __LITTLE_ENDIAN
	u16 start_of_ts;
	u16 seconds_hi;
#else /* __ BIG_ENDIAN */
#error Big Endian platforms are unsupported
#endif
	u32 seconds;
	u32 nanoseconds;
	u32 rtp_ts;
} __packed;

#define RA_PTP_EXTTS_TIMESTAMP_WORDLEN \
	ALIGN(sizeof(struct ra_ptp_extts_fpga_timestamp), 4)

#define RA_PTP_EXTTS_START_OF_TS		0x1588
#define RA_PTP_EXTTS_MAX_TS_CNT			15

#define RA_PTP_DEFAULT_FPGA_CLOCK_FREQ		125000000
#define RA_PTP_DEFAULT_PER_OUT_INTERVAL		312500

#define RA_PTP_EXTTS_CNT			1 // 1 external event as timestamp trigger
#define RA_PTP_PEROUT_CNT			1 // 1 periodic output

#define RA_PTP_DRIFT_CORRECTION_MAX_PPB		100000

#define RA_PTP_IRQS				0x0000
#define RA_PTP_IRQ_EXTTS			BIT(0)
#define RA_PTP_IRQ_PPS				BIT(1)
#define RA_PTP_IRQ_DISABLE			BIT(2)

#define RA_PTP_ID				0x0008
#define RA_PTP_ID_VALUE				0x15880000
#define RA_PTP_ID_MASK				0xffff0000
#define RA_PTP_ID_PPS_AVAILABLE			BIT(0)

#define RA_PTP_CMD				0x000c
#define RA_PTP_CMD_WRITE_CLOCK			BIT(0)
#define RA_PTP_CMD_READ_CLOCK			BIT(1)
#define RA_PTP_CMD_APPLY_DRIFT_CORRECTION	BIT(2)
#define RA_PTP_CMD_APPLY_CLOCK_OFFSET		BIT(3)
#define RA_PTP_CMD_ACK_PPS_IRQ			BIT(4)
#define RA_PTP_CMD_RESET_EXTTS_FIFO_OVFLW	BIT(8)

#define RA_PTP_STATUS				0x0010
#define RA_PTP_STATUS_READ_CLOCK_VALID		BIT(1)
#define RA_PTP_STATUS_EXTTS_FIFO_OVFLW		BIT(8)

#define RA_PTP_SET_TIME_SECONDS_H		0x0014
#define RA_PTP_SET_TIME_SECONDS			0x0018
#define RA_PTP_SET_TIME_NANOSECONDS		0x001c

#define RA_PTP_DRIFT_CORRECTION			0x0020
#define RA_PTP_DRIFT_CORRECTION_NEGATIVE	0x80000000
#define RA_PTP_DRIFT_CORRECTION_PPB_VALUE_MASK	0x0007ffff

#define RA_PTP_OFFSET_CORRECTION		0x0024
#define RA_PTP_OFFSET_CORRECTION_NEGATIVE	0x80000000
#define RA_PTP_OFFSET_CORRECTION_NS_VALUE_MASK	0x3fffffff

#define RA_PTP_READ_TIME_SECONDS_H		0x0028
#define RA_PTP_READ_TIME_SECONDS		0x002c
#define RA_PTP_READ_TIME_NANOSECONDS		0x0030

#define RA_PTP_PPS_LENGTH			0x0034

#define RA_PTP_EVENT_OUT_SECONDS_H		0x0038
#define RA_PTP_EVENT_OUT_SECONDS		0x003c
#define RA_PTP_EVENT_OUT_NANOSECONDS		0x0040

#define RA_PTP_EVENT_OUT_MODE			0x0044
#define RA_PTP_EVENT_OUT_MODE_PERIODIC		0x1
#define RA_PTP_EVENT_OUT_MODE_ENABLE		0x2 // must be disabled to change event out time

#define RA_PTP_EVENT_OUT_NS_INTERVAL		0x0048
#define RA_PTP_EVENT_OUT_NS_INTERVAL_VALUE_MASK	0x3fffffff

#define RA_PTP_EXTTS_MODE			0x004c
#define RA_PTP_EXTTS_MODE_EVENT_CNT_VALUE_MASK	0x000fffff
#define RA_PTP_EXTTS_MODE_APPEND_SEQUENCE_CNT	0x40000000 // append sequence id instead of RTP Timestamp at ext. timestamp
#define RA_PTP_EXTTS_MODE_ENABLE_EXTTS		0x80000000

#define RA_PTP_EXTTS_TS_CNT			0x0050	// number of timestamps in "external timstamp" fifo

#define RA_PTP_EXTTS_DATA			0x0100	// timestamp packet fifo

#endif /* RA_PTP_REGS */
