package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"net"
	"time"
	"unsafe"
)

type RxStream struct {
	index       int
	device      *Device
	description RxStreamDescription
}

type RxStreamDescription struct {
	PrimaryDestination, SecondaryDestination net.UDPAddr

	Active            bool
	SyncSource        bool
	HitlessProtection bool
	Synchronous       bool
	RtpFilter         bool

	CodecType      Codec
	RtpPayloadType uint8

	JitterBufferMargin uint16

	RtpOffset uint32
	RtpSsrc   uint32

	NumChannels uint16

	// Tracks is a map of channel index to track index.
	// The maximum number of tracks is reported in the device's Info struct.
	Tracks [MaxChannels]int16
}

func (sd *RxStreamDescription) toIoctlStruct() []byte {
	buf := new(bytes.Buffer)

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.PrimaryDestination.IP))
	binary.Write(buf, binary.BigEndian, uint16(sd.PrimaryDestination.Port))
	binary.Write(buf, binary.LittleEndian, [2]uint8{0}) // padding

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.SecondaryDestination.IP))
	binary.Write(buf, binary.BigEndian, uint16(sd.SecondaryDestination.Port))
	binary.Write(buf, binary.LittleEndian, [2]uint8{0}) // padding

	binary.Write(buf, binary.LittleEndian, sd.SyncSource)
	binary.Write(buf, binary.LittleEndian, 0) // reserved
	binary.Write(buf, binary.LittleEndian, sd.HitlessProtection)
	binary.Write(buf, binary.LittleEndian, sd.Synchronous)
	binary.Write(buf, binary.LittleEndian, sd.RtpFilter)

	binary.Write(buf, binary.LittleEndian, sd.CodecType)
	binary.Write(buf, binary.LittleEndian, sd.RtpPayloadType)

	binary.Write(buf, binary.LittleEndian, sd.Active)

	binary.Write(buf, binary.LittleEndian, 0) // reserved
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

func (rx *RxStream) ReadRTCP(timeout time.Duration) (RxRTCPData, error) {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(rx.index)) // index
	binary.Write(buf, binary.LittleEndian, uint32(timeout.Milliseconds()))
	binary.Write(buf, binary.LittleEndian, [3 + (2 * 9)]uint32{}) // padding for return data
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirRead|ioctlDirWrite, 'r', 0x10, uintptr(len(b)))
	if err := doIoctl(rx.device.f.Fd(), code, p); err != nil {
		return RxRTCPData{}, err
	}

	return rxRTCPFromIoctlStruct(b[8:]), nil
}
