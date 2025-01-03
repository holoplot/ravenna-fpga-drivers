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

	lastSubSamples subSamples
	lastOffset     uint32
	sampleRate     *big.Int
}

const (
	subSamplesPerSample          = 8
	subSamplesDeviationThreshold = 2
)

var nanoSecondsPerSecond = big.NewInt(int64(time.Second / time.Nanosecond))

type subSamples uint64

// toSamples converts a subSamples value to samples by dividing by subSamplesPerSample.
// If the remainder is greater than or equal to half of the number of subSamplesPerSample,
// the result is rounded up.
func (s subSamples) toSamples32() uint32 {
	i := uint64(s) / subSamplesPerSample

	if uint64(s)%subSamplesPerSample >= subSamplesPerSample/2 {
		i++
	}

	return uint32(i)
}

// diff returns the difference between two subSamples values.
func (s subSamples) diff(other subSamples) int64 {
	return int64(s) - int64(other)
}

// diffAbs returns the absolute difference between two subSamples values.
func (s subSamples) diffAbs(other subSamples) int64 {
	diff := s.diff(other)
	if diff < 0 {
		return -diff
	}

	return diff
}

// subtractSamples subtracts a number of samples from a subSamples value.
func (s subSamples) subtractSamples(i uint32) subSamples {
	return s - subSamples(i*subSamplesPerSample)
}

// ptpTimestampToRtpTimestamp converts a PTP timestamp to an rtpTimestamp.
// The PTP timestamp is given in nanoseconds, and the result is represented in samples and quarter samples.
func (s *RtpSyncer) ptpTimestampToSubSamples(ptpTimestamp uint64) subSamples {
	// subSamples = subSamplesPerSample * ptpTimestamp * sampleRate / nanoSecondsPerSecond

	r := big.NewInt(0)
	r.Mul(big.NewInt(int64(ptpTimestamp)), s.sampleRate)
	r.Mul(r, big.NewInt(subSamplesPerSample))
	r.Div(r, nanoSecondsPerSecond)

	return subSamples(r.Uint64())
}

type UpdateFunc func(ctx context.Context, s *RtpSyncer, oldOffset, newOffset uint32)

// Run starts the RTP periodic synchronization process.
// The interval parameter specifies the time between two synchronization steps. A typical value is 1 second.
//
// At each iteration, a pair of PTP and local media timestamps is read from the FPGA. The PTP time tells us
// now many nanoseconds have passed since the PTP epoch, and the local media time tells us the current sample
// counter in the FPGA. We use these numbers to calculate the offset the FPGA needs to apply when it indexes
// samples in its buffer. If the difference between the new and the last calculated offset is more than 1
// half-sample, the FPGA register for the global RTP offset is updated and the callback is called with the
// old and new offset.
//
// The computations are done in subSamples to compensate for rounding errors due to integer division.
//
// If there are any TX or syntonous RX streams, those will need to be restarted by the caller if the offset
// changes by more than 2.
func (s *RtpSyncer) Run(ctx context.Context, interval time.Duration, cb UpdateFunc) error {
	for {
		select {
		case <-time.After(interval):
			ptpTimestamp, localMediaTime, err := s.pd.GetTimestampPair()
			if err != nil {
				log.Error().Err(err).Msg("Failed to read timestamps")

				continue
			}

			subs := s.ptpTimestampToSubSamples(ptpTimestamp).
				subtractSamples(localMediaTime)

			log.Debug().
				Uint64("ptpTimestamp", ptpTimestamp).
				Uint32("localMediaTime", localMediaTime).
				Uint64("subSamples", uint64(subs)).
				Int64("diffSubSamples", subs.diff(s.lastSubSamples)).
				Uint32("offset", subs.toSamples32()).
				Msg("Timestamps")

			if subs.diffAbs(s.lastSubSamples) >= subSamplesDeviationThreshold {
				offset := subs.toSamples32()

				if offset != s.lastOffset {
					if err := s.nd.SetRTPGlobalOffset(uint64(offset)); err != nil {
						return fmt.Errorf("failed to set timestamp: %w", err)
					}

					cb(ctx, s, uint32(s.lastOffset), uint32(offset))
				}

				s.lastOffset = offset
			}

			s.lastSubSamples = subs

		case <-ctx.Done():
			return ctx.Err()
		}
	}
}

// New creates a new RtpSyncer instance.
//
// The FPGA has an internal, free-running clock that represents the the media time.
// This clock's frequency is synchronized with the PTP clock by the FPGA, but its phase
// in multiple of samples is is arbitrary and depends on the boot time of the FPGA.
// In order to synchronize the device to the PTP network, the media time needs to be
// adjusted by the RTP global offset. Refer to the comments in the Run() method for more
// details.
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
		sampleRate: big.NewInt(int64(sampleRate)),
		lastOffset: uint32(n),
	}, nil
}
