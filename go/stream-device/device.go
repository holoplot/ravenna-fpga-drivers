package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io/fs"
	"os"
	"unsafe"
)

type DeviceInfo struct {
	MaxTracks    int
	MaxRxStreams int
	MaxTxStreams int
}

type Device struct {
	f    *os.File
	info DeviceInfo
}

func Open(path string) (*Device, error) {
	d := &Device{}

	var err error

	if d.f, err = os.OpenFile(path, 0, fs.FileMode(os.O_RDWR)); err != nil {
		return nil, err
	}

	if err := d.readInfo(); err != nil {
		return nil, fmt.Errorf("failed to read device info: %w", err)
	}

	return d, nil
}

func (d *Device) Info() DeviceInfo {
	return d.info
}

func (d *Device) readInfo() error {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0))   // version
	binary.Write(buf, binary.LittleEndian, [3]uint32{}) // padding for return data
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirRead|ioctlDirWrite, 'r', 0x00, uintptr(len(b)))
	if err := doIoctl(d.f.Fd(), code, p); err != nil {
		return err
	}

	v := uint32(0)

	binary.Read(buf, binary.LittleEndian, &v)
	// ignore version

	binary.Read(buf, binary.LittleEndian, &v)
	d.info.MaxTracks = int(v)

	binary.Read(buf, binary.LittleEndian, &v)
	d.info.MaxRxStreams = int(v)

	binary.Read(buf, binary.LittleEndian, &v)
	d.info.MaxTxStreams = int(v)

	return nil
}

func (d *Device) AddRxStream(sd RxStreamDescription) (*RxStream, error) {
	rx := RxStream{
		device:      d,
		description: sd,
	}

	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	buf.Write(sd.toIoctlStruct())
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x30, uintptr(len(b)))
	err := doIoctl(d.f.Fd(), code, p)

	if err != nil {
		return nil, err
	}

	return &rx, nil
}

func (d *Device) updateRxStream(rx *RxStream, sd RxStreamDescription) error {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	binary.Write(buf, binary.LittleEndian, uint32(rx.index))
	buf.Write(sd.toIoctlStruct())
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x31, uintptr(len(b)))
	return doIoctl(d.f.Fd(), code, p)
}

func (d *Device) deleteRxStream(rx *RxStream) error {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	binary.Write(buf, binary.LittleEndian, uint32(rx.index))
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x32, uintptr(len(b)))
	return doIoctl(d.f.Fd(), code, p)
}

func (d *Device) AddTxStream(sd TxStreamDescription) (*TxStream, error) {
	tx := TxStream{
		device:      d,
		description: sd,
	}

	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	buf.Write(sd.toIoctlStruct())
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x20, uintptr(len(b)))
	err := doIoctl(d.f.Fd(), code, p)
	if err != nil {
		return nil, err
	}

	return &tx, nil
}

func (d *Device) updateTxStream(tx *TxStream, sd TxStreamDescription) error {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	binary.Write(buf, binary.LittleEndian, uint32(tx.index))
	buf.Write(sd.toIoctlStruct())
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x21, uintptr(len(b)))
	return doIoctl(d.f.Fd(), code, p)
}

func (d *Device) deleteTxStream(tx *TxStream) error {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.LittleEndian, uint32(0)) // version
	binary.Write(buf, binary.LittleEndian, uint32(tx.index))
	b := buf.Bytes()
	p := unsafe.Pointer(&b[0])

	code := ioctlMakeCode(ioctlDirWrite, 'r', 0x22, uintptr(len(b)))
	return doIoctl(d.f.Fd(), code, p)
}
