package main

import (
	"flag"
	"log/slog"
	"os"

	"github.com/holoplot/ravenna-fpga-drivers/go/internal/logger"
	rpd "github.com/holoplot/ravenna-fpga-drivers/go/ptp-device"
)

func main() {
	deviceIndexFlag := flag.Int("index", 0, "PTP device index")
	debugFlag := flag.Bool("debug", false, "Enable debug log")
	flag.Parse()

	logger.Setup(*debugFlag)

	pd := rpd.New(*deviceIndexFlag)

	ptpTimestamp, rtpTimestamp, err := pd.GetTimestampPair()
	if err != nil {
		slog.Error("Failed to read timestamps", "error", err)
		os.Exit(1)
	}

	slog.Info("Timestamps read",
		"ptpTimestamp", ptpTimestamp,
		"rtpTimestamp", rtpTimestamp)
}
