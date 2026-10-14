package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"net"
	"time"
	"unsafe"
)

type TxStream struct {
	index       int
	device      *Device
	description TxStreamDescription
}

type TxStreamDescriptionNetworkInterface struct {
	Destination, Source net.UDPAddr
	VlanTag             uint16
	DestinationMAC      net.HardwareAddr
}

type TxStreamDescription struct {
	Primary, Secondary TxStreamDescriptionNetworkInterface

	Active       bool
	Multicast    bool
	VlanTagged   bool
	UsePrimary   bool
	UseSecondary bool

	CodecType  Codec
	NumSamples uint8

	RtpPayloadType     uint8
	NextRtpTxTime      uint8
	NextRtpSequenceNum uint16

	Ttl     uint8
	DscpTos uint8

	RtpOffset uint32
	RtpSsrc   uint32

	NumChannels uint16

	// Tracks is a map of channel index to track index.
	// The maximum number of tracks is defined in the device's Info struct.
	Tracks [MaxChannels]int16
}

func (ni *TxStreamDescriptionNetworkInterface) toIoctlStruct() ([]byte, error) {
	dstIP, dstPort, err := udpAddrToBytes(ni.Destination)
	if err != nil {
		return nil, fmt.Errorf("destination: %w", err)
	}

	srcIP, srcPort, err := udpAddrToBytes(ni.Source)
	if err != nil {
		return nil, fmt.Errorf("source: %w", err)
	}

	mac, err := macToBytes(ni.DestinationMAC)
	if err != nil {
		return nil, fmt.Errorf("destination MAC: %w", err)
	}

	buf := new(bytes.Buffer)

	binary.Write(buf, binary.BigEndian, dstIP)
	binary.Write(buf, binary.BigEndian, srcIP)
	binary.Write(buf, binary.BigEndian, dstPort)
	binary.Write(buf, binary.BigEndian, srcPort)
	binary.Write(buf, binary.BigEndian, ni.VlanTag)
	binary.Write(buf, binary.BigEndian, mac)

	return buf.Bytes(), nil
}

func (sd *TxStreamDescription) toIoctlStruct() ([]byte, error) {
	pri, err := sd.Primary.toIoctlStruct()
	if err != nil {
		return nil, fmt.Errorf("primary: %w", err)
	}

	sec, err := sd.Secondary.toIoctlStruct()
	if err != nil {
		return nil, fmt.Errorf("secondary: %w", err)
	}

	buf := new(bytes.Buffer)

	buf.Write(pri)
	buf.Write(sec)

	binary.Write(buf, binary.LittleEndian, sd.VlanTagged)
	binary.Write(buf, binary.LittleEndian, sd.Multicast)
	binary.Write(buf, binary.LittleEndian, sd.UsePrimary)
	binary.Write(buf, binary.LittleEndian, sd.UseSecondary)

	binary.Write(buf, binary.LittleEndian, sd.CodecType)
	binary.Write(buf, binary.LittleEndian, sd.RtpPayloadType)
	binary.Write(buf, binary.LittleEndian, sd.NextRtpTxTime)
	binary.Write(buf, binary.LittleEndian, sd.Ttl)
	binary.Write(buf, binary.LittleEndian, sd.DscpTos)
	binary.Write(buf, binary.LittleEndian, sd.NumSamples)

	binary.Write(buf, binary.LittleEndian, sd.Active)

	binary.Write(buf, binary.LittleEndian, [1]uint8{0}) // padding

	binary.Write(buf, binary.LittleEndian, sd.NextRtpSequenceNum)
	binary.Write(buf, binary.LittleEndian, sd.NumChannels)

	binary.Write(buf, binary.LittleEndian, sd.RtpOffset)
	binary.Write(buf, binary.LittleEndian, sd.RtpSsrc)

	binary.Write(buf, binary.LittleEndian, sd.Tracks)

	return buf.Bytes(), nil
}

func (tx *TxStream) Index() int {
	return tx.index
}

func (tx *TxStream) Description() TxStreamDescription {
	return tx.description
}

func (tx *TxStream) Update(sd TxStreamDescription) error {
	if err := tx.device.updateTxStream(tx, sd); err != nil {
		return err
	}

	tx.description = sd

	return nil
}

func (tx *TxStream) Close() error {
	return tx.device.deleteTxStream(tx)
}

func (tx *TxStream) ReadRTCP(timeout time.Duration) (TxRTCPData, error) {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(tx.index)) // index
	binary.Write(buf, binary.LittleEndian, uint32(timeout.Milliseconds()))
	binary.Write(buf, binary.LittleEndian, [1 + (2 * 2)]uint32{}) // padding for return data
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirRead|ioctlDirWrite, 'r', 0x11, len(b))
	if err := doIoctl(tx.device.f.Fd(), code, p); err != nil {
		return TxRTCPData{}, err
	}

	return txRTCPFromIoctlStruct(b[8:]), nil
}
