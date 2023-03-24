package syncer

import (
	"context"
	"fmt"
	"time"

	"github.com/holoplot/go-linuxptp/pkg/ptp"
	rnd "github.com/holoplot/ravenna-fpga-drivers/go/network-device"
	rpd "github.com/holoplot/ravenna-fpga-drivers/go/ptp-device"
	"github.com/rs/zerolog/log"
	"lukechampine.com/uint128"
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

	r := uint128.From64(ptpTimestamp)
	r = r.Mul64(uint64(s.sampleRate))

	div := uint64(time.Second / time.Nanosecond)
	remainder := r.Mod64(div)

	r = r.Div64(div)

	if r.Cmp64(remainder*2) > 0 {
		r = r.Add64(1)
	}

	r = r.Sub64(uint64(rtpTimestamp))

	return uint32(r.Lo)
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
		lastOffset: uint32(n),
	}, nil
}
