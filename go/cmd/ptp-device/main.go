package main

import (
	"flag"

	rpd "github.com/holoplot/ravenna-fpga-drivers/go/ptp-device"
	"github.com/mattn/go-colorable"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	deviceIndexFlag := flag.Int("index", 0, "PTP device index")
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	pd := rpd.New(*deviceIndexFlag)

	ptpTimestamp, rtpTimestamp, err := pd.GetTimestampPair()
	if err != nil {
		log.Fatal().Msg("Failed to read timestamps")
	}

	log.Info().
		Uint64("ptpTimestamp", ptpTimestamp).
		Uint32("rtpTimestamp", rtpTimestamp).
		Msg("Timestamps read")
}
