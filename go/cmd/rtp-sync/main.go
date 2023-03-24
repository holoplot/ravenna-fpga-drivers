package main

import (
	"context"
	"flag"
	"time"

	rs "github.com/holoplot/ravenna-fpga-drivers/go/rtp-syncer"
	"github.com/mattn/go-colorable"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	netDeviceIndexFlag := flag.Int("net-index", 0, "Network device index")
	sampleRateFlag := flag.Int("sample-rate", 48000, "Sample rate")
	debugFlag := flag.Bool("debug", false, "Enable debug log")
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	if *debugFlag {
		zerolog.SetGlobalLevel(zerolog.DebugLevel)
	}

	syncer, err := rs.New(*netDeviceIndexFlag, *sampleRateFlag)
	if err != nil {
		log.Fatal().Err(err).Msg("Failed to create RTP syncer")
	}

	log.Info().Msg("Starting monitor")

	ctx := context.Background()

	if err := syncer.Run(ctx, time.Second, func(ctx context.Context, s *rs.RtpSyncer, oldOffset, newOffset uint32) {
		log.Info().
			Uint32("old-offset", oldOffset).
			Uint32("new-offset", newOffset).
			Msg("RTP/PTP offset updated")
	}); err != nil {
		log.Fatal().Err(err).Msg("Failed to run RTP syncer")
	}

	<-ctx.Done()
}
