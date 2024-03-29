package ravenna_stream_device

import (
	"errors"
	"fmt"
	"syscall"
	"unsafe"
)

const (
	ioctlDirNone  = 0x0
	ioctlDirWrite = 0x1
	ioctlDirRead  = 0x2
)

func doIoctlWithRet(fd uintptr, code uint32, ptr unsafe.Pointer) (int, error) {
	ret, _, errno := syscall.Syscall(syscall.SYS_IOCTL, fd, uintptr(code), uintptr(ptr))
	if errno != 0 {
		return 0, errors.New(errno.Error())
	}

	return int(ret), nil
}

func doIoctl(fd uintptr, code uint32, ptr unsafe.Pointer) error {
	_, err := doIoctlWithRet(fd, code, ptr)

	return err
}

func ioctlMakeCode(dir, typ, nr int, size uintptr) uint32 {
	var code uint32
	if dir > ioctlDirWrite|ioctlDirRead {
		panic(fmt.Errorf("invalid ioctl dir value: %d", dir))
	}

	if size > 1<<14 {
		panic(fmt.Errorf("invalid ioctl size value: %d", size))
	}

	code |= uint32(dir) << 30
	code |= uint32(size) << 16
	code |= uint32(typ) << 8
	code |= uint32(nr)

	return code
}
