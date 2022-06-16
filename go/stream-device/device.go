package ravenna_stream_device

import (
	"bytes"
	"encoding/binary"
	"io/fs"
	"os"
	"unsafe"
)

type Device struct {
	f *os.File
}

func Open(path string) (*Device, error) {
	f, err := os.OpenFile(path, 0, fs.FileMode(os.O_RDWR))
	if err != nil {
		return nil, err
	}

	return &Device{
		f: f,
	}, nil
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
