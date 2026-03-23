package main

import (
	"flag"
	"log/slog"
	"net"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/holoplot/ravenna-fpga-drivers/go/internal/logger"
	rsd "github.com/holoplot/ravenna-fpga-drivers/go/stream-device"
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
	rtpOffsetFlag := flag.Int("rtp-offset", 0, "RTP offset")
	rtpSsrcFlag := flag.Int("rtp-ssrc", 1, "RTP Sync Source Identifier")
	rtpPayloadTypeFlag := flag.Int("rtp-payload-type", 98, "RTP payload type")
	trackMapFlag := flag.String("track-map", "", "Comma separated list of tracks to map. Defaults to 1:1 mapping to channels.")
	debugFlag := flag.Bool("debug", false, "Enable debug log")
	flag.Parse()

	logger.Setup(*debugFlag)

	if (*primaryDestinationIpFlag == "" || *primarySourceIpFlag == "") &&
		(*secondaryDestinationIpFlag == "" || *secondarySourceIpFlag == "") {
		slog.Error("-pri-src-ip/-pri-dst-ip and/or -sec-src-ip/-sec-dst-ip must be passed")

		os.Exit(1)
	}

	sd, err := rsd.Open(*deviceFileFlag)
	if err != nil {
		slog.Error("Unable to open device file", "error", err, "path", *deviceFileFlag)
		os.Exit(1)
	}

	slog.Info("Device file opened",
		"max-tracks", sd.Info().MaxTracks,
		"max-rx-streams", sd.Info().MaxRxStreams,
		"max-tx-streams", sd.Info().MaxTxStreams,
		"path", *deviceFileFlag)

	txDesc := rsd.TxStreamDescription{
		Active:         true,
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

			if n, err := strconv.Atoi(t); err == nil && n < sd.Info().MaxTracks {
				txDesc.Tracks[i] = int16(n)
			}
		}
	}

	tx, err := sd.AddTxStream(txDesc)
	if err != nil {
		slog.Error("Unable to add TX stream", "error", err)

		os.Exit(1)
	}

	slog.Info("TX stream added",
		"channels", txDesc.NumChannels,
		"samples", txDesc.NumSamples,
		"ttl", txDesc.Ttl,
		"primary", txDesc.UsePrimary,
		"secondary", txDesc.UseSecondary,
		"primary-destination-ip", txDesc.Primary.Destination.IP,
		"primary-source-ip", txDesc.Primary.Source.IP,
		"primary-port", txDesc.Primary.Destination.Port,
		"secondary-destination-ip", txDesc.Secondary.Destination.IP,
		"secondary-source-ip", txDesc.Secondary.Source.IP,
		"secondary-port", txDesc.Secondary.Destination.Port,
		"primary-vlan", txDesc.Primary.VlanTag,
		"secondary-vlan", txDesc.Secondary.VlanTag,
		"rtp-offset", txDesc.RtpOffset,
		"rtp-ssrc", txDesc.RtpSsrc,
		"rtp-payload-type", txDesc.RtpPayloadType,
		"multicast", txDesc.Multicast,
		"tx-tracks", txDesc.Tracks[:txDesc.NumChannels])

	slog.Info("Hit ^C to exit.")

	for {
		time.Sleep(time.Second)

		if r, err := tx.ReadRTCP(time.Second); err == nil {
			slog.Info("RTCP general data", "rtp-timestamp", r.RtpTimestamp)

			logInterfaceData := func(i rsd.TxRTCPInterfaceData, msg string) {
				slog.Info(msg,
					"sent-packets", i.SentPackets,
					"sent-rtp-bytes", i.SentRTPBytes)
			}

			logInterfaceData(r.Primary, "RTCP data primary")

			if txDesc.UseSecondary {
				logInterfaceData(r.Secondary, "RTCP data secondary")
			}
		} else {
			slog.Error("Error reading RTCP", "error", err)
		}
	}
}
