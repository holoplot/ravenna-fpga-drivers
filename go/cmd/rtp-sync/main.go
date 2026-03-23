package main

import (
	"context"
	"flag"
	"log/slog"
	"os"
	"time"

	"github.com/holoplot/ravenna-fpga-drivers/go/internal/logger"
	rs "github.com/holoplot/ravenna-fpga-drivers/go/rtp-syncer"
)

func main() {
	netDeviceIndexFlag := flag.Int("net-index", 0, "Network device index")
	sampleRateFlag := flag.Int("sample-rate", 48000, "Sample rate")
	debugFlag := flag.Bool("debug", false, "Enable debug log")
	flag.Parse()

	logger.Setup(*debugFlag)

	syncer, err := rs.New(*netDeviceIndexFlag, *sampleRateFlag)
	if err != nil {
		slog.Error("Failed to create RTP syncer", "error", err)

		os.Exit(1)
	}

	slog.Info("Starting monitor")

	ctx := context.Background()

	if err := syncer.Run(ctx, time.Second, func(ctx context.Context, s *rs.RtpSyncer, oldOffset, newOffset uint32) {
		slog.Info("RTP/PTP offset updated",
			"old-offset", oldOffset,
			"new-offset", newOffset)
	}); err != nil {
		slog.Error("Failed to run RTP syncer", "error", err)

		os.Exit(1)
	}

	<-ctx.Done()
}
