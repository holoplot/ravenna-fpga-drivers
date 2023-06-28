package syncer

import (
	"context"
	"fmt"
	"math/big"
	"time"

	"github.com/holoplot/go-linuxptp/pkg/ptp"
	rnd "github.com/holoplot/ravenna-fpga-drivers/go/network-device"
	rpd "github.com/holoplot/ravenna-fpga-drivers/go/ptp-device"
	"github.com/rs/zerolog/log"
)

type RtpSyncer struct {
	pd *rpd.Device
	nd *rnd.Device

	lastOffset uint32
	sampleRate int
}

// globalWordClockCounter returns the current offset between PTP and RTP, in samples
func (s *RtpSyncer) globalWordClockCounter(ptpTimestamp uint64, rtpTimestamp uint32) uint32 {
	// v = (ptpTimestamp * sampleRate / nanoSecondsPerSecond) - rtpTimestamp

	r := big.NewInt(0)
	r.Mul(big.NewInt(int64(ptpTimestamp)), big.NewInt(int64(s.sampleRate)))

	div := big.NewInt(int64(time.Second / time.Nanosecond))
	remainder := big.NewInt(0)
	r.DivMod(r, div, remainder)

	remainder.Mul(remainder, big.NewInt(2))

	if remainder.Cmp(div) > 0 {
		r.Add(r, big.NewInt(1))
	}

	r.Sub(r, big.NewInt(int64(rtpTimestamp)))
	r.And(r, big.NewInt(0xffffffff))

	return uint32(r.Int64())
}

type UpdateFunc func(ctx context.Context, s *RtpSyncer, oldOffset, newOffset uint32)

func (s *RtpSyncer) Run(ctx context.Context, interval time.Duration, cb UpdateFunc) error {
	for {
		select {
		case <-time.After(interval):
			ptpTimestamp, rtpTimestamp, err := s.pd.GetTimestampPair()
			if err != nil {
				log.Error().Err(err).Msg("Failed to read timestamps")

				continue
			}

			offset := s.globalWordClockCounter(ptpTimestamp, rtpTimestamp)

			if offset > s.lastOffset+1 || offset < s.lastOffset-1 {
				if err := s.nd.SetRTPGlobalOffset(uint64(offset)); err != nil {
					return fmt.Errorf("failed to set timestamp: %w", err)
				} else {
					cb(ctx, s, s.lastOffset, offset)
					s.lastOffset = offset
				}
			}

		case <-ctx.Done():
			return ctx.Err()
		}
	}
}

func New(netDeviceIndex, sampleRate int) (*RtpSyncer, error) {
	nd := rnd.New(netDeviceIndex)

	clockIndex, err := nd.GetPTPClockIndex()
	if err != nil {
		return nil, fmt.Errorf("failed to get ptp clock index: %w", err)
	}

	pd := rpd.New(clockIndex)

	ptpDevice, err := ptp.Open(clockIndex)
	if err != nil {
		return nil, fmt.Errorf("failed to open PTP device: %w", err)
	}

	if err := ptpDevice.RequestExternalTimestamp(0, ptp.ExternalTimestampEnable|ptp.ExternalTimestampRisingEdge); err != nil {
		return nil, fmt.Errorf("failed to enable external timestamping: %w", err)
	}

	n, err := nd.GetRTPGlobalOffset()
	if err != nil {
		return nil, fmt.Errorf("failed to read current RTP offset: %w", err)
	}

	return &RtpSyncer{
		pd:         pd,
		nd:         nd,
		sampleRate: sampleRate,
		lastOffset: uint32(n),
	}, nil
}
