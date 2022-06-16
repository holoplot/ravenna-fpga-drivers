package ravenna_stream_device

import "net"

func ipToBytes(ip net.IP) [4]byte {
	var b [4]byte
	copy(b[:], net.IP.To4(ip))
	return b
}

func macToBytes(mac net.HardwareAddr) [6]byte {
	var b [6]byte
	copy(b[:], mac)
	return b
}
