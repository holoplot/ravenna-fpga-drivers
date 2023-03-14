package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
)

type RxRTCPInterfaceData struct {
	MisorderedPackets       uint16
	BaseSequenceNr          uint16
	ExtendedMaxSequenceNr   uint32
	ReceivedPackets         uint32
	PeakJitter              uint16
	EstimatedJitter         uint16
	LastTransitTime         uint16
	CurrentOffsetEstimation uint16
	LastSsrc                uint32
	BufferMarginMin         uint16
	BufferMarginMax         uint16
	LatePackets             uint16
	EarlyPackets            uint16
	TimeoutCounter          uint16

	Error   bool
	Playing bool
}

type RxRTCPData struct {
	RtpTimestamp     uint32
	DevState         uint8
	RtpPayloadId     uint8
	OffsetEstimation uint16
	PathDifferential int32

	Primary, Secondary RxRTCPInterfaceData
}

func rxRTCPFromIoctlStruct(b []byte) RxRTCPData {
	buf := bytes.NewBuffer(b)
	d := RxRTCPData{}

	binary.Read(buf, binary.LittleEndian, &d.RtpTimestamp)
	binary.Read(buf, binary.LittleEndian, &d.DevState)
	binary.Read(buf, binary.LittleEndian, &d.RtpPayloadId)
	binary.Read(buf, binary.LittleEndian, &d.OffsetEstimation)
	binary.Read(buf, binary.LittleEndian, &d.PathDifferential)

	binary.Read(buf, binary.LittleEndian, &d.Primary.MisorderedPackets)
	binary.Read(buf, binary.LittleEndian, &d.Primary.BaseSequenceNr)
	binary.Read(buf, binary.LittleEndian, &d.Primary.ExtendedMaxSequenceNr)
	binary.Read(buf, binary.LittleEndian, &d.Primary.ReceivedPackets)
	binary.Read(buf, binary.LittleEndian, &d.Primary.PeakJitter)
	binary.Read(buf, binary.LittleEndian, &d.Primary.EstimatedJitter)
	binary.Read(buf, binary.LittleEndian, &d.Primary.LastTransitTime)
	binary.Read(buf, binary.LittleEndian, &d.Primary.CurrentOffsetEstimation)
	binary.Read(buf, binary.LittleEndian, &d.Primary.LastSsrc)
	binary.Read(buf, binary.LittleEndian, &d.Primary.BufferMarginMin)
	binary.Read(buf, binary.LittleEndian, &d.Primary.BufferMarginMax)
	binary.Read(buf, binary.LittleEndian, &d.Primary.LatePackets)
	binary.Read(buf, binary.LittleEndian, &d.Primary.EarlyPackets)
	binary.Read(buf, binary.LittleEndian, &d.Primary.TimeoutCounter)
	binary.Read(buf, binary.LittleEndian, &d.Primary.Error)
	binary.Read(buf, binary.LittleEndian, &d.Primary.Playing)

	binary.Read(buf, binary.LittleEndian, &d.Secondary.MisorderedPackets)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.BaseSequenceNr)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.ExtendedMaxSequenceNr)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.ReceivedPackets)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.PeakJitter)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.EstimatedJitter)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.LastTransitTime)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.CurrentOffsetEstimation)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.LastSsrc)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.BufferMarginMin)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.BufferMarginMax)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.LatePackets)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.EarlyPackets)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.TimeoutCounter)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.Error)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.Playing)

	return d
}
