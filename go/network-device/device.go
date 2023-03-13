package ravenna_network_device

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/safchain/ethtool"
)

type Device struct {
	index int
}

func New(index int) *Device {
	return &Device{
		index: index,
	}
}

func (d *Device) openAttributeFile(name string, flags int) (*os.File, error) {
	fname := fmt.Sprintf("/sys/class/net/ra%d/ra_net/%s", d.index, name)
	return os.OpenFile(fname, flags, 0)
}

func (d *Device) readStringAttribute(name string) (string, error) {
	f, err := d.openAttributeFile(name, os.O_RDONLY)
	if err != nil {
		return "", fmt.Errorf("open failed: %w", err)
	}

	defer f.Close()

	b, err := io.ReadAll(f)
	if err != nil {
		return "", fmt.Errorf("read failed: %w", err)
	}

	s := string(b)
	s = strings.TrimSpace(s)
	s = strings.TrimSuffix(s, "\n")

	return s, nil
}

func (d *Device) readNumericAttribute(name string) (uint64, error) {
	s, err := d.readStringAttribute(name)
	if err != nil {
		return 0, err
	}

	i, err := strconv.ParseUint(string(s), 10, 64)
	if err != nil {
		return 0, fmt.Errorf("numeric conversion failed: %w", err)
	}

	return i, nil
}

func (d *Device) writeStringAttribute(name, s string) error {
	f, err := d.openAttributeFile(name, os.O_WRONLY)
	if err != nil {
		return fmt.Errorf("open failed: %w", err)
	}

	defer f.Close()

	_, err = f.Write([]byte(s))
	if err != nil {
		return fmt.Errorf("write failed: %w", err)
	}

	return nil
}

func (d *Device) writeNumericAttribute(name string, v uint64) error {
	s := fmt.Sprintf("%d", v)
	return d.writeStringAttribute(name, s)
}

func (d *Device) GetRavCoreVersion() (string, error) {
	return d.readStringAttribute("rav_core_version")
}

func (d *Device) GetDriverVersion() (string, error) {
	return d.readStringAttribute("driver_version")
}

func (d *Device) GetUDPThrottledPackets() (uint64, error) {
	return d.readNumericAttribute("udp_throttled_packets")
}

func (d *Device) GetRxPacketsParsed() (uint64, error) {
	return d.readNumericAttribute("rx_packets_parsed")
}

func (d *Device) GetRxQueueErrors() (uint64, error) {
	return d.readNumericAttribute("rx_queue_errors")
}

func (d *Device) GetRxChecksumErrors() (uint64, error) {
	return d.readNumericAttribute("rx_checksum_errors")
}

func (d *Device) GetRxStreamPacketsDropped() (uint64, error) {
	return d.readNumericAttribute("rx_stream_packets_dropped")
}

func (d *Device) GetRxStreamPackets() (uint64, error) {
	return d.readNumericAttribute("rx_stream_packets")
}

func (d *Device) GetRxLegacyPackets() (uint64, error) {
	return d.readNumericAttribute("rx_legacy_packets")
}

func (d *Device) GetTxStreamPackets() (uint64, error) {
	return d.readNumericAttribute("tx_stream_packets")
}

func (d *Device) GetTxLegacyPackets() (uint64, error) {
	return d.readNumericAttribute("tx_legacy_packets")
}

func (d *Device) GetTxStreamPacketsLost() (uint64, error) {
	return d.readNumericAttribute("tx_stream_packets_lost")
}

func (d *Device) GetRTPGlobalOffset() (uint64, error) {
	return d.readNumericAttribute("rtp_global_offset")
}

func (d *Device) SetRTPGlobalOffset(offset uint64) error {
	return d.writeNumericAttribute("rtp_global_offset", offset)
}

func (d *Device) GetUDPFilterPort() (uint16, error) {
	if port, err := d.readNumericAttribute("udp_filter_port"); err == nil {
		return uint16(port), nil
	} else {
		return 0, err
	}
}

func (d *Device) SetUDPFilterPort(port uint16) error {
	return d.writeNumericAttribute("udp_filter_port", uint64(port))
}

func (d *Device) ResetCounter() error {
	return d.writeStringAttribute("counter_reset", "")
}

func (d *Device) GetPTPClockIndex() (int, error) {
	e, err := ethtool.NewEthtool()
	if err != nil {
		panic(err.Error())
	}
	defer e.Close()

	name := fmt.Sprintf("ra%d", d.index)

	ts, err := e.GetTimestampingInformation(name)
	if err != nil {
		return 0, fmt.Errorf("failed to read timestamping information: %w", err)
	}

	return int(ts.PhcIndex), nil
}
