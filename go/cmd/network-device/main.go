package main

import (
	"flag"

	rnd "github.com/holoplot/ravenna-fpga-drivers/go/network-device"
	"github.com/mattn/go-colorable"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	deviceIndexFlag := flag.Int("index", 0, "Network device index")
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	nd := rnd.New(*deviceIndexFlag)

	ravCoreVersion, err := nd.GetRavCoreVersion()
	if err != nil {
		log.Fatal().Str("method", "GetRavCoreVersion").Msg("Failed to read attribute")
	}

	driverVersion, err := nd.GetDriverVersion()
	if err != nil {
		log.Fatal().Str("method", "GetDriverVersion").Msg("Failed to read attribute")
	}

	udpThrottledPackets, err := nd.GetUDPThrottledPackets()
	if err != nil {
		log.Fatal().Str("method", "GetUdpThrottledPackets").Msg("Failed to read attribute")
	}

	rxPacketsParsed, err := nd.GetRxPacketsParsed()
	if err != nil {
		log.Fatal().Str("method", "GetRxPacketsParsed").Msg("Failed to read attribute")
	}

	rxQueueErrors, err := nd.GetRxQueueErrors()
	if err != nil {
		log.Fatal().Str("method", "GetRxQueueErrors").Msg("Failed to read attribute")
	}

	rxChecksumErrors, err := nd.GetRxChecksumErrors()
	if err != nil {
		log.Fatal().Str("method", "GetRxChecksumErrors").Msg("Failed to read attribute")
	}

	rxStreamPacketsDropped, err := nd.GetRxStreamPacketsDropped()
	if err != nil {
		log.Fatal().Str("method", "GetRxStreamPacketsDropped").Msg("Failed to read attribute")
	}

	rxStreamPackets, err := nd.GetRxStreamPackets()
	if err != nil {
		log.Fatal().Str("method", "GetRxStreamPackets").Msg("Failed to read attribute")
	}

	rxLegacyPackets, err := nd.GetRxLegacyPackets()
	if err != nil {
		log.Fatal().Str("method", "GetRxLegacyPackets").Msg("Failed to read attribute")
	}

	txStreamPackets, err := nd.GetTxStreamPackets()
	if err != nil {
		log.Fatal().Str("method", "GetTxStreamPackets").Msg("Failed to read attribute")
	}

	txLegacyPackets, err := nd.GetTxLegacyPackets()
	if err != nil {
		log.Fatal().Str("method", "GetTxLegacyPackets").Msg("Failed to read attribute")
	}

	txStreamPacketsLost, err := nd.GetTxStreamPacketsLost()
	if err != nil {
		log.Fatal().Str("method", "GetTxStreamPacketsLost").Msg("Failed to read attribute")
	}

	rtpGlobalOffset, err := nd.GetRTPGlobalOffset()
	if err != nil {
		log.Fatal().Str("method", "GetRTPGlobalOffset").Msg("Failed to read attribute")
	}

	udpFilterPort, err := nd.GetUDPFilterPort()
	if err != nil {
		log.Fatal().Str("method", "GetUDPFilterPort").Msg("Failed to read attribute")
	}

	log.Info().
		Str("ravCoreVersion", ravCoreVersion).
		Str("driverVersion", driverVersion).
		Uint64("udpThrottledPackets", udpThrottledPackets).
		Uint64("rxPacketsParsed", rxPacketsParsed).
		Uint64("rxQueueErrors", rxQueueErrors).
		Uint64("rxChecksumErrors", rxChecksumErrors).
		Uint64("rxStreamPacketsDropped", rxStreamPacketsDropped).
		Uint64("rxStreamPackets", rxStreamPackets).
		Uint64("rxLegacyPackets", rxLegacyPackets).
		Uint64("txStreamPackets", txStreamPackets).
		Uint64("txLegacyPackets", txLegacyPackets).
		Uint64("txStreamPacketsLost", txStreamPacketsLost).
		Uint64("rtpGlobalOffset", rtpGlobalOffset).
		Uint16("udpFilterPort", udpFilterPort).
		Msg("Device attributes read")
}
