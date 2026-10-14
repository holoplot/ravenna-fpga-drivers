package ravenna_stream_device

import (
	"fmt"
	"net"
)

// ipToBytes encodes an IPv4 address. An empty address means "unused" and
// encodes as 0.0.0.0. Anything else must be IPv4.
func ipToBytes(ip net.IP) ([4]byte, error) {
	var b [4]byte

	if len(ip) == 0 {
		return b, nil
	}

	ip4 := ip.To4()
	if ip4 == nil {
		return b, fmt.Errorf("not an IPv4 address: %s", ip)
	}

	copy(b[:], ip4)

	return b, nil
}

func portToUint16(port int) (uint16, error) {
	if port < 0 || port > 0xffff {
		return 0, fmt.Errorf("invalid UDP port: %d", port)
	}

	return uint16(port), nil
}

func udpAddrToBytes(addr net.UDPAddr) ([4]byte, uint16, error) {
	ip, err := ipToBytes(addr.IP)
	if err != nil {
		return ip, 0, err
	}

	port, err := portToUint16(addr.Port)

	return ip, port, err
}

// macToBytes encodes a MAC address. An empty address encodes as all zeros.
func macToBytes(mac net.HardwareAddr) ([6]byte, error) {
	var b [6]byte

	if len(mac) == 0 {
		return b, nil
	}

	if len(mac) != len(b) {
		return b, fmt.Errorf("not an Ethernet MAC address: %s", mac)
	}

	copy(b[:], mac)

	return b, nil
}
