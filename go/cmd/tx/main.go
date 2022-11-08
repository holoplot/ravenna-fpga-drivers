package main

import (
	"flag"
	"net"
	"strconv"
	"strings"
	"time"

	rsd "github.com/holoplot/ravenna-fpga-drivers/go/stream-device"
	"github.com/mattn/go-colorable"
	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"
)

func main() {
	deviceFileFlag := flag.String("dev", "/dev/ravenna-stream-device", "Device file to use")
	channelsFlag := flag.Int("channels", 2, "Number of channels to send")
	samplesFlag := flag.Int("samples", 8, "Number of samples to send per packet")
	primaryDestinationIpFlag := flag.String("pri-dst-ip", "", "Primary destination IP")
	secondaryDestinationIpFlag := flag.String("sec-dst-ip", "", "Secondary destination IP")
	primarySourceIpFlag := flag.String("pri-src-ip", "", "Primary source IP")
	secondarySourceIpFlag := flag.String("sec-src-ip", "", "Secondary source IP")
	primaryDestinationPortFlag := flag.Int("pri-dst-port", 5004, "Primary destination UDP port")
	secondaryDestinationPortFlag := flag.Int("sec-dst-port", 5004, "Secondary destination UDP port")
	primarySourcePortFlag := flag.Int("pri-src-port", 5004, "Primary source UDP port")
	secondarySourcePortFlag := flag.Int("sec-src-port", 5004, "Secondary source UDP port")
	primaryVlanTagFlag := flag.Int("pri-vlan", 0, "VLAN tag for primary stream")
	secondaryVlanTagFlag := flag.Int("sec-vlan", 0, "VLAN tag for secondary stream")
	ttlFlag := flag.Int("ttl", 1, "TTL of outgoing packets")
	nextRtpTxTimeFlag := flag.Int("next-rtp-tx-time", 0, "Next RTP TX time")
	nextRtpSequenceNumberFlag := flag.Int("next-rtp-sequence-number", 0, "Next RTP sequence number")
	rtpOffsetFlag := flag.Int("rtp-offset", 500, "RTP offset")
	rtpSsrcFlag := flag.Int("rtp-ssrc", 1, "RTP Sync Source Identifier")
	rtpPayloadTypeFlag := flag.Int("rtp-payload-type", 97, "RTP payload type")
	trackMapFlag := flag.String("track-map", "", "Comma separated list of tracks to map. Defaults to 1:1 mapping to channels.")
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	if (*primaryDestinationIpFlag == "" || *primarySourceIpFlag == "") &&
		(*secondaryDestinationIpFlag == "" || *secondarySourceIpFlag == "") {
		log.Fatal().Msg("-pri-src-ip/-pri-dst-ip and/or -sec-src-ip/-sec-dst-ip must be passed")
	}

	sd, err := rsd.Open(*deviceFileFlag)
	if err != nil {
		log.Fatal().
			Err(err).
			Str("path", *deviceFileFlag).
			Msg("Unable to open device file")
	}

	txDesc := rsd.TxStreamDescription{
		CodecType:      rsd.StreamCodecL24,
		RtpPayloadType: uint8(*rtpPayloadTypeFlag),

		RtpOffset: uint32(*rtpOffsetFlag),
		RtpSsrc:   uint32(*rtpSsrcFlag),

		NumChannels: uint16(*channelsFlag),
		NumSamples:  uint8(*samplesFlag),

		Ttl:                uint8(*ttlFlag),
		NextRtpTxTime:      uint8(*nextRtpTxTimeFlag),
		NextRtpSequenceNum: uint16(*nextRtpSequenceNumberFlag),
	}

	if len(*primaryDestinationIpFlag) > 0 || len(*primarySourceIpFlag) > 0 {
		txDesc.Primary.Destination.IP = net.ParseIP(*primaryDestinationIpFlag)
		txDesc.Primary.Source.IP = net.ParseIP(*primarySourceIpFlag)
		txDesc.Primary.Destination.Port = *primaryDestinationPortFlag
		txDesc.Primary.Source.Port = *primarySourcePortFlag
		txDesc.Primary.VlanTag = uint16(*primaryVlanTagFlag)
		txDesc.UsePrimary = true
	}

	if len(*secondaryDestinationIpFlag) > 0 || len(*secondarySourceIpFlag) > 0 {
		txDesc.Secondary.Destination.IP = net.ParseIP(*secondaryDestinationIpFlag)
		txDesc.Secondary.Source.IP = net.ParseIP(*secondarySourceIpFlag)
		txDesc.Secondary.Destination.Port = *secondaryDestinationPortFlag
		txDesc.Secondary.Source.Port = *secondarySourcePortFlag
		txDesc.Secondary.VlanTag = uint16(*secondaryVlanTagFlag)
		txDesc.UseSecondary = true
	}

	txDesc.Multicast = txDesc.Primary.Destination.IP.IsMulticast() || txDesc.Secondary.Destination.IP.IsMulticast()
	txDesc.VlanTagged = txDesc.Primary.VlanTag > 0 || txDesc.Secondary.VlanTag > 0

	if *trackMapFlag == "" {
		for i := uint16(0); i < txDesc.NumChannels; i++ {
			txDesc.Tracks[i] = int16(i)
		}
	} else {
		for i := uint16(0); i < txDesc.NumChannels; i++ {
			txDesc.Tracks[i] = rsd.TrackNull
		}

		for i, t := range strings.Split(*trackMapFlag, ",") {
			if i >= int(txDesc.NumChannels) {
				break
			}

			if n, err := strconv.Atoi(t); err == nil && n < rsd.MaxTracks {
				txDesc.Tracks[i] = int16(n)
			}
		}
	}

	_, err = sd.AddTxStream(txDesc)
	if err != nil {
		log.Fatal().
			Err(err).
			Msg("Unable to add TX stream")
	}

	log.Info().
		Int("channels", int(txDesc.NumChannels)).
		Int("samples", int(txDesc.NumSamples)).
		Int("ttl", int(txDesc.Ttl)).
		Bool("primary", txDesc.UsePrimary).
		Bool("secondary", txDesc.UseSecondary).
		IPAddr("primary-destination-ip", txDesc.Primary.Destination.IP).
		IPAddr("primary-source-ip", txDesc.Primary.Source.IP).
		Int("primary-port", int(txDesc.Primary.Destination.Port)).
		IPAddr("secondary-destination-ip", txDesc.Secondary.Destination.IP).
		IPAddr("secondary-source-ip", txDesc.Secondary.Source.IP).
		Int("secondary-port", int(txDesc.Secondary.Destination.Port)).
		Int("primary-vlan", int(txDesc.Primary.VlanTag)).
		Int("secondary-vlan", int(txDesc.Secondary.VlanTag)).
		Int("rtp-offset", int(txDesc.RtpOffset)).
		Int("rtp-ssrc", int(txDesc.RtpSsrc)).
		Int("rtp-payload-type", int(txDesc.RtpPayloadType)).
		Bool("multicast", txDesc.Multicast).
		Ints16("tx-tracks", txDesc.Tracks[:txDesc.NumChannels]).
		Msg("TX stream added")

	log.Info().Msg("Hit ^C to exit.")

	for {
		time.Sleep(time.Minute)
	}
}
