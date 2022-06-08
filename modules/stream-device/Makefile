ifeq ($(MODULE),)
	MODULE := ravenna-stream-device
endif

obj-m := $(MODULE).o

$(MODULE)-y += \
	main.o \
	debugfs.o \
	rtcp.o \
	rx.o \
	tx.o \
	stream-table-rx.o \
	stream-table-tx.o \
	track-table.o
