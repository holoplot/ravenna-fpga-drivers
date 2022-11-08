package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"net"
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
	Tracks      [MaxTracks]int16
}

func (sd *TxStreamDescription) toIoctlStruct() []byte {
	buf := new(bytes.Buffer)

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Primary.Destination.IP))
	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Primary.Source.IP))
	binary.Write(buf, binary.BigEndian, uint16(sd.Primary.Destination.Port))
	binary.Write(buf, binary.BigEndian, uint16(sd.Primary.Source.Port))
	binary.Write(buf, binary.BigEndian, sd.Primary.VlanTag)
	binary.Write(buf, binary.BigEndian, macToBytes(sd.Primary.DestinationMAC))

	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Secondary.Destination.IP))
	binary.Write(buf, binary.BigEndian, ipToBytes(sd.Secondary.Source.IP))
	binary.Write(buf, binary.BigEndian, uint16(sd.Secondary.Destination.Port))
	binary.Write(buf, binary.BigEndian, uint16(sd.Secondary.Source.Port))
	binary.Write(buf, binary.BigEndian, sd.Secondary.VlanTag)
	binary.Write(buf, binary.BigEndian, macToBytes(sd.Secondary.DestinationMAC))

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

	binary.Write(buf, binary.LittleEndian, [2]uint8{0}) // padding

	binary.Write(buf, binary.LittleEndian, sd.NextRtpSequenceNum)
	binary.Write(buf, binary.LittleEndian, sd.NumChannels)

	binary.Write(buf, binary.LittleEndian, sd.RtpOffset)
	binary.Write(buf, binary.LittleEndian, sd.RtpSsrc)

	binary.Write(buf, binary.LittleEndian, sd.Tracks)

	return buf.Bytes()
}

func (Tx *TxStream) Update(sd TxStreamDescription) error {
	return Tx.device.updateTxStream(Tx, sd)
}

func (Tx *TxStream) Close() error {
	return Tx.device.deleteTxStream(Tx)
}
