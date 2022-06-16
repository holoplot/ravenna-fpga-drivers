package main

import (
	"flag"
	"net"
	"time"

	rsd "github.com/holoplot/ravenna-fpga-drivers/go/stream-device"
	"github.com/mattn/go-colorable"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	deviceFileFlag := flag.String("dev", "/dev/ravenna-stream-device", "Device file to use")
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	sd, err := rsd.Open(*deviceFileFlag)
	if err != nil {
		log.Fatal().
			Err(err).
			Str("path", *deviceFileFlag).
			Msg("Unable to open device file")
	}

	rxDesc := rsd.RxStreamDescription{
		Primary: rsd.RxStreamDescriptionNetworkInterface{
			DestinationIP:   net.ParseIP("228.196.73.186"),
			DestinationPort: 5004,
		},
		SyncSource:        false,
		VlanTagged:        false,
		HitlessProtection: false,
		Synchronous:       false,
		RtpFilter:         false,

		CodecType:      rsd.StreamCodecL24,
		RtpPayloadType: 97,

		VlanTag:            0,
		JitterBufferMargin: 500,

		RtpOffset: 500,
		RtpSsrc:   0,

		NumChannels: 2,
	}

	for i := uint16(0); i < rxDesc.NumChannels; i++ {
		rxDesc.Tracks[i] = int16(i)
	}

	rxStream, err := sd.AddRxStream(rxDesc)
	if err != nil {
		log.Error().
			Err(err).
			Msg("Unable to add RX stream")
	}

	log.Info().Msg("RX stream added")

	mac, _ := net.ParseMAC("00:01:02:03:04:05")

	txDesc := rsd.TxStreamDescription{
		Primary: rsd.TxStreamDescriptionNetworkInterface{
			DestinationIP:   net.ParseIP("192.168.0.1"),
			SourceIP:        net.ParseIP("10.0.0.1"),
			DestinationMAC:  mac,
			SourcePort:      1024,
			DestinationPort: 5004,
		},
		Multicast:    false,
		UsePrimary:   true,
		UseSecondary: false,

		/* RA_STREAM_CODEC_... */
		CodecType:  rsd.StreamCodecL24,
		NumSamples: 16,

		RtpPayloadType:     97,
		NextRtpTxTime:      0,
		NextRtpSequenceNum: 123,

		Ttl:     8,
		DscpTos: 16,

		RtpOffset: 123,
		RtpSsrc:   123,

		NumChannels: 8,
	}

	for i := uint16(0); i < txDesc.NumChannels; i++ {
		if i%2 == 1 {
			txDesc.Tracks[i] = int16(i)
		} else {
			txDesc.Tracks[i] = rsd.TrackNull
		}
	}

	txStream, err := sd.AddTxStream(txDesc)
	if err != nil {
		log.Error().
			Err(err).
			Msg("Unable to add TX stream")
	}

	log.Info().Msg("TX stream added")

	time.Sleep(time.Minute)

	rxStream.Close()
	log.Info().Msg("RX stream closed")

	time.Sleep(time.Minute)

	txStream.Close()
	log.Info().Msg("TX stream closed")

	time.Sleep(time.Minute)
}
