package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
)

type TxRTCPInterfaceData struct {
	SentPackets  uint32
	SentRTPBytes uint32
}

type TxRTCPData struct {
	RtpTimestamp uint32

	Primary, Secondary TxRTCPInterfaceData
}

func txRTCPFromIoctlStruct(b []byte) TxRTCPData {
	buf := bytes.NewBuffer(b)
	d := TxRTCPData{}

	binary.Read(buf, binary.LittleEndian, &d.RtpTimestamp)

	binary.Read(buf, binary.LittleEndian, &d.Primary.SentPackets)
	binary.Read(buf, binary.LittleEndian, &d.Primary.SentRTPBytes)

	binary.Read(buf, binary.LittleEndian, &d.Secondary.SentPackets)
	binary.Read(buf, binary.LittleEndian, &d.Secondary.SentRTPBytes)

	return d
}
