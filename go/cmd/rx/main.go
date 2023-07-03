package main

import (
	"context"
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
	flag.Parse()

	consoleWriter := zerolog.ConsoleWriter{
		Out: colorable.NewColorableStdout(),
	}

	log.Logger = log.Output(consoleWriter)

	if *primaryIpFlag == "" && *secondaryIpFlag == "" {
		log.Fatal().Msg("-pri-ip or -sec-ip must be passed")
	}

	sd, err := rsd.Open(*deviceFileFlag)
	if err != nil {
		log.Fatal().
			Err(err).
			Str("path", *deviceFileFlag).
			Msg("Unable to open device file")
	}

	log.Info().
		Int("max-tracks", sd.Info().MaxTracks).
		Int("max-rx-streams", sd.Info().MaxRxStreams).
		Int("max-tx-streams", sd.Info().MaxTxStreams).
		Str("path", *deviceFileFlag).
		Msg("Device file opened")

	rxDesc := rsd.RxStreamDescription{
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
				log.Info().
					Str("interface", ifiName).
					IPAddr("ip", addr.IP).
					Int("port", addr.Port).
					Msg("Listening")

				go func() {
					<-ctx.Done()
					l.Close()
				}()
			} else {
				log.Error().
					Err(err).
					Str("interface", ifiName).
					IPAddr("ip", addr.IP).
					Int("port", addr.Port).
					Msg("Cannot listen")
			}
		} else {
			log.Error().
				Err(err).
				Str("interface", ifiName).
				Msg("Unable to lookup network interface")
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

			if n, err := strconv.Atoi(t); err == nil && n < rsd.MaxTracks {
				rxDesc.Tracks[i] = int16(n)
			}
		}
	}

	rx, err := sd.AddRxStream(rxDesc)
	if err != nil {
		log.Fatal().
			Err(err).
			Msg("Unable to add RX stream")
	}

	log.Info().
		Uint16("channels", rxDesc.NumChannels).
		IPAddr("primary-ip", rxDesc.PrimaryDestination.IP).
		Int("primary-port", rxDesc.PrimaryDestination.Port).
		IPAddr("secondary-ip", rxDesc.SecondaryDestination.IP).
		Int("secondary-port", rxDesc.SecondaryDestination.Port).
		Uint16("jitter-buffer-margin", rxDesc.JitterBufferMargin).
		Uint32("rtp-offset", rxDesc.RtpOffset).
		Uint32("rtp-ssrc", rxDesc.RtpSsrc).
		Uint8("rtp-payload-type", rxDesc.RtpPayloadType).
		Bool("rtp-filter", rxDesc.RtpFilter).
		Bool("synchronous", rxDesc.Synchronous).
		Bool("sync-source", rxDesc.SyncSource).
		Bool("hitless", rxDesc.HitlessProtection).
		Ints16("rx-tracks", rxDesc.Tracks[:rxDesc.NumChannels]).
		Msg("RX stream added")

	log.Info().Msg("Hit ^C to exit.")

	for {
		time.Sleep(time.Second)

		if r, err := rx.ReadRTCP(time.Second); err == nil {
			log.Info().
				Uint32("rtp-timestamp", r.RtpTimestamp).
				Uint8("dev-state", r.DevState).
				Uint8("rtp-payload-id", r.RtpPayloadId).
				Uint16("offset-estimation", r.OffsetEstimation).
				Int32("path-differential", r.PathDifferential).
				Msg("RTCP general")

			logInterfaceData := func(i rsd.RxRTCPInterfaceData, msg string) {
				log.Info().
					Uint16("misordered-packets", i.MisorderedPackets).
					Uint16("base-sequence-nr", i.BaseSequenceNr).
					Uint32("extended-max-sequence-nr", i.ExtendedMaxSequenceNr).
					Uint32("received-packets", i.ReceivedPackets).
					Uint16("peak-jitter", i.PeakJitter).
					Uint16("estimated-jitter", i.EstimatedJitter).
					Uint16("last-transit-time", i.LastTransitTime).
					Uint16("current-offset-estimation", i.CurrentOffsetEstimation).
					Uint32("last-ssrc", i.LastSsrc).
					Uint16("buffer-margin-min", i.BufferMarginMin).
					Uint16("buffer-margin-max", i.BufferMarginMax).
					Uint16("late-packets", i.LatePackets).
					Uint16("early-packets", i.EarlyPackets).
					Uint16("timeout-counter", i.TimeoutCounter).
					Bool("playing", i.Playing).
					Bool("error", i.Error).
					Msg(msg)
			}

			logInterfaceData(r.Primary, "RTCP primary")

			if len(*secondaryIpFlag) > 0 {
				logInterfaceData(r.Secondary, "RTCP secondary")
			}
		} else {
			log.Error().Err(err).Msg("Error reading RTCP")
		}
	}
}
