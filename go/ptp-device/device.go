package ravenna_ptp_device

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
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
	fname := fmt.Sprintf("/sys/class/ptp/ptp%d/device/%s", d.index, name)
	return os.OpenFile(fname, flags, 0)
}

func (d *Device) GetTimestampPair() (uint64, uint32, error) {
	f, err := d.openAttributeFile("rtp_timestamp", os.O_RDONLY)
	if err != nil {
		return 0, 0, fmt.Errorf("open failed: %w", err)
	}

	defer f.Close()

	b, err := io.ReadAll(f)
	if err != nil {
		return 0, 0, fmt.Errorf("read failed: %w", err)
	}

	s := string(b)
	s = strings.TrimSpace(s)
	s = strings.TrimSuffix(s, "\n")

	a := strings.Split(s, " ")
	if len(a) != 2 {
		return 0, 0, fmt.Errorf("malformed payload: %s", s)
	}

	ptpTimestamp, err := strconv.ParseUint(a[0], 10, 64)
	if err != nil {
		return 0, 0, fmt.Errorf("conversion failed: %w", err)
	}

	localMediaTime, err := strconv.ParseUint(a[1], 10, 32)
	if err != nil {
		return 0, 0, fmt.Errorf("conversion failed: %w", err)
	}

	return ptpTimestamp, uint32(localMediaTime), nil
}
