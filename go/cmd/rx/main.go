package main

import (
	"context"
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
	channelsFlag := flag.Int("channels", 2, "Number of channels to receive")
	primaryIpFlag := flag.String("pri-ip", "", "Primary destination IP")
	secondaryIpFlag := flag.String("sec-ip", "", "Secondary destination IP")
	primaryPortFlag := flag.Int("pri-port", 5004, "Primary UDP port")
	secondaryPortFlag := flag.Int("sec-port", 5004, "Secondary UDP port")
	jitterBufferMarginFlag := flag.Int("jitter-buffer-margin", 500, "Jitter buffer margin")
	rtpOffsetFlag := flag.Int("rtp-offset", 500, "RTP offset")
	rtpSsrcFlag := flag.Int("rtp-ssrc", 0, "RTP Sync Source Identifier")
	vlanTagFlag := flag.Int("vlan", -1, "VLAN tag")
	rtpPayloadTypeFlag := flag.Int("rtp-payload-type", 98, "RTP payload type")
	synchronousFlag := flag.Bool("synchronous", false, "Use synchronous instead of syntonous")
	syncSourceFlag := flag.Bool("sync-source", false, "Use stream as sync source")
	rtpFilterFlag := flag.Bool("rtp-filter", false, "Use RTP filter")
	hitlessFlag := flag.Bool("hitless", false, "Hitless protection")
	trackMapFlag := flag.String("track-map", "", "Comma separated list of tracks to map. Defaults to 1:1 mapping to channels.")
	debugFlag := flag.Bool("debug", false, "Enable debug log")
	flag.Parse()

	logger.Setup(*debugFlag)

	if *primaryIpFlag == "" && *secondaryIpFlag == "" {
		slog.Error("-pri-ip or -sec-ip must be passed")

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

	rxDesc := rsd.RxStreamDescription{
		Active:            true,
		SyncSource:        *syncSourceFlag,
		HitlessProtection: *hitlessFlag,

		Synchronous: *synchronousFlag,
		RtpFilter:   *rtpFilterFlag,

		CodecType:      rsd.StreamCodecL24,
		RtpPayloadType: uint8(*rtpPayloadTypeFlag),

		JitterBufferMargin: uint16(*jitterBufferMarginFlag),

		RtpOffset: uint32(*rtpOffsetFlag),
		RtpSsrc:   uint32(*rtpSsrcFlag),

		NumChannels: uint16(*channelsFlag),
	}

	listenMulticast := func(ctx context.Context, ifiName string, addr net.UDPAddr) {
		if ifi, err := net.InterfaceByName(ifiName); err == nil {
			if l, err := net.ListenMulticastUDP("udp4", ifi, &addr); err == nil {
				slog.Info("Listening",
					"interface", ifiName,
					"ip", addr.IP,
					"port", addr.Port)

				go func() {
					<-ctx.Done()
					l.Close()
				}()
			} else {
				slog.Error("Cannot listen",
					"error", err,
					"interface", ifiName,
					"ip", addr.IP,
					"port", addr.Port)
			}
		} else {
			slog.Error("Unable to lookup network interface",
				"error", err,
				"interface", ifiName)
		}
	}

	ctx := context.Background()

	if len(*primaryIpFlag) > 0 {
		rxDesc.PrimaryDestination = net.UDPAddr{
			IP:   net.ParseIP(*primaryIpFlag),
			Port: *primaryPortFlag,
		}

		listenMulticast(ctx, "ra0", rxDesc.PrimaryDestination)
	}

	if len(*secondaryIpFlag) > 0 {
		rxDesc.SecondaryDestination = net.UDPAddr{
			IP:   net.ParseIP(*secondaryIpFlag),
			Port: *secondaryPortFlag,
		}

		listenMulticast(ctx, "ra1", rxDesc.SecondaryDestination)
	}

	if *vlanTagFlag >= 0 {
		rxDesc.VlanTag = uint16(*vlanTagFlag)
		rxDesc.VlanTagged = true
	}

	if *trackMapFlag == "" {
		for i := uint16(0); i < rxDesc.NumChannels; i++ {
			rxDesc.Tracks[i] = int16(i)
		}
	} else {
		for i := uint16(0); i < rxDesc.NumChannels; i++ {
			rxDesc.Tracks[i] = rsd.TrackNull
		}

		for i, t := range strings.Split(*trackMapFlag, ",") {
			if i >= int(rxDesc.NumChannels) {
				break
			}

			if n, err := strconv.Atoi(t); err == nil && n < sd.Info().MaxTracks {
				rxDesc.Tracks[i] = int16(n)
			}
		}
	}

	rx, err := sd.AddRxStream(rxDesc)
	if err != nil {
		slog.Error("Unable to add RX stream", "error", err)

		os.Exit(1)
	}

	slog.Info("RX stream added",
		"channels", rxDesc.NumChannels,
		"primary-ip", rxDesc.PrimaryDestination.IP,
		"primary-port", rxDesc.PrimaryDestination.Port,
		"secondary-ip", rxDesc.SecondaryDestination.IP,
		"secondary-port", rxDesc.SecondaryDestination.Port,
		"jitter-buffer-margin", rxDesc.JitterBufferMargin,
		"rtp-offset", rxDesc.RtpOffset,
		"rtp-ssrc", rxDesc.RtpSsrc,
		"rtp-payload-type", rxDesc.RtpPayloadType,
		"rtp-filter", rxDesc.RtpFilter,
		"synchronous", rxDesc.Synchronous,
		"sync-source", rxDesc.SyncSource,
		"hitless", rxDesc.HitlessProtection,
		"rx-tracks", rxDesc.Tracks[:rxDesc.NumChannels])

	slog.Info("Hit ^C to exit.")

	for {
		time.Sleep(time.Second)

		if r, err := rx.ReadRTCP(time.Second); err == nil {
			slog.Info("RTCP general",
				"rtp-timestamp", r.RtpTimestamp,
				"dev-state", r.DevState,
				"rtp-payload-id", r.RtpPayloadId,
				"offset-estimation", r.OffsetEstimation,
				"path-differential", r.PathDifferential)

			logInterfaceData := func(i rsd.RxRTCPInterfaceData, msg string) {
				slog.Info(msg,
					"misordered-packets", i.MisorderedPackets,
					"base-sequence-nr", i.BaseSequenceNr,
					"extended-max-sequence-nr", i.ExtendedMaxSequenceNr,
					"received-packets", i.ReceivedPackets,
					"peak-jitter", i.PeakJitter,
					"estimated-jitter", i.EstimatedJitter,
					"last-transit-time", i.LastTransitTime,
					"current-offset-estimation", i.CurrentOffsetEstimation,
					"last-ssrc", i.LastSsrc,
					"buffer-margin-min", i.BufferMarginMin,
					"buffer-margin-max", i.BufferMarginMax,
					"late-packets", i.LatePackets,
					"early-packets", i.EarlyPackets,
					"timeout-counter", i.TimeoutCounter,
					"playing", i.Playing,
					"error", i.Error)
			}

			logInterfaceData(r.Primary, "RTCP primary")

			if len(*secondaryIpFlag) > 0 {
				logInterfaceData(r.Secondary, "RTCP secondary")
			}
		} else {
			slog.Error("Error reading RTCP", "error", err)
		}
	}
}
