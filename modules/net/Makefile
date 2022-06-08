ifeq ($(MODULE),)
	MODULE := ravenna-net
endif

obj-m := $(MODULE).o

$(MODULE)-y += main.o ethtool.o phylink.o sysfs.o timestamp.o mdio.o

