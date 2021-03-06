package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"net"
)

type RxStream struct {
	index       int
	device      *Device
	description RxStreamDescription
}

type RxStreamDescriptionNetworkInterface struct {
	DestinationIP   net.IP
	DestinationPort uint16
}

type RxStreamDescription struct {
	Primary, Secondary RxStreamDescriptionNetworkInterface

	SyncSource        bool
	VlanTagged        bool
	HitlessProtection bool
	Synchronous       bool
	RtpFilter         bool

	CodecType      Codec
	RtpPayloadType uint8

	VlanTag            uint16
	JitterBufferMargin uint16

	RtpOffset uint32
	RtpSsrc   uint32

	NumChannels uint16
	Tracks      [MaxTracks]int16
}

func (sd *RxStreamDescription) toIoctlStruct() []byte {
	buf := new(bytes.Buffer)

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Primary.DestinationIP))
	binary.Write(buf, binary.BigEndian, sd.Primary.DestinationPort)
	binary.Write(buf, binary.LittleEndian, [2]uint8{0}) // padding

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Secondary.DestinationIP))
	binary.Write(buf, binary.BigEndian, sd.Secondary.DestinationPort)
	binary.Write(buf, binary.LittleEndian, [2]uint8{0}) // padding

	binary.Write(buf, binary.LittleEndian, sd.SyncSource)
	binary.Write(buf, binary.LittleEndian, sd.VlanTagged)
	binary.Write(buf, binary.LittleEndian, sd.HitlessProtection)
	binary.Write(buf, binary.LittleEndian, sd.Synchronous)
	binary.Write(buf, binary.LittleEndian, sd.RtpFilter)

	binary.Write(buf, binary.LittleEndian, sd.CodecType)
	binary.Write(buf, binary.LittleEndian, sd.RtpPayloadType)

	binary.Write(buf, binary.LittleEndian, [1]uint8{0})

	binary.Write(buf, binary.LittleEndian, sd.VlanTag)
	binary.Write(buf, binary.LittleEndian, sd.JitterBufferMargin)

	binary.Write(buf, binary.LittleEndian, sd.RtpOffset)
	binary.Write(buf, binary.LittleEndian, sd.RtpSsrc)

	binary.Write(buf, binary.LittleEndian, sd.NumChannels)

	binary.Write(buf, binary.LittleEndian, sd.Tracks)

	binary.Write(buf, binary.LittleEndian, [2]uint8{0})

	return buf.Bytes()
}

func (rx *RxStream) Update(sd RxStreamDescription) error {
	return rx.device.updateRxStream(rx, sd)
}

func (rx *RxStream) Close() error {
	return rx.device.deleteRxStream(rx)
}
