# Kernel driver modules for LAWO's Ravenna FPGA implementation

This repository contains the kernel driver modules for LAWO's Ravenna FPGA
implementation.

## IRQ

This driver supports the IRQ mux. It supports the 16bit and 32bit variants and
offers as many IRQs as the register width. All other drivers in this project
are connected to one of the IRQ muxes.

### DTS properties

| Property name                          | Mandatory | Description                                |
|----------------------------------------|:---------:|--------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-irq-controller-32bit` or `lawo,ravenna-irq-controller-16bit` |
| `interrupt-controller`                 | *         |                                            |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                         |
| `#interrupt-cells`                     | *         | Set to `<1>`                               |
| `#address-cells`                       | *         | Set to `<1>`                               |
| `reg`                                  | *         | Register space                             |

### Example DTS binding

```
    ravenna_irq: ravenna-irq@a01ffc00 {
        compatible = "lawo,ravenna-irq-controller-32bit";

        interrupt-controller;
        interrupt-parent = <&gic>;
        interrupts = <0 89 4>;
        #interrupt-cells = <1>;
        #address-cells = <1>;
        reg = <0x0 0xa01ffc00 0x0 0x10>;
    };
```

## Network

This driver supports the network interfaces and exposed them as Linux network interfaces.

### Statistics

The driver exposes a number of non-standard statistics through the `ethtool` API.
Users can use `ethtool -S <device>` to read the statistics.

### SysFS entries

Some more non-standard configuration can be read and written through the sysfs interface.

| Entry name				 | Access    | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `rtp_global_offset`                    | R/W       | `RA_NET_RTP_GLOBAL_OFFSET`                  |
| `counter_reset`                        | W/O       | `RA_NET_PP_CNT_RST`                         |

### DTS properties

| Property name                           | Mandatory | Description                                 |
|-----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                            | *         | Must be `lawo,ravenna-ethernet`             |
| `interrupts`, `interrupt-parent`        | *         | Upstream interrupt                          |
| `interrupt-names`                       | *         | Must be `pp`                                |
| `reg`                                   | *         | Register space                              |
| `phy-handle`                            | *         | PHY handle to use                           |
| `phy-mode`                              |           | PHY mode to set                             |
| `lawo-ptp-clock`                        |           | phandle to the Ravenna PTP clock node       |
| `lawo,ptp-delay-path-rx-1000mbit-nsec`  |           | RX path delay in 1000 Mbit/s mode, in nsecs |
| `lawo,ptp-delay-path-rx-100mbit-nsec`   |           | RX path delay in 100 Mbit/s mode, in nsecs  |
| `lawo,ptp-delay-path-rx-10mbit-nsec`    |           | RX path delay in 10 Mbit/s mode, in nsecs   |
| `lawo,ptp-delay-path-tx-nsec`           |           | TX path delay for all modes, in nsecs       |

### Example DTS binding:

```
    ra0: ravenna-net@a0180000 {
        compatible = "lawo,ravenna-ethernet";
        reg = <0x0 0xa0180000 0x0 0x1000>;

        interrupt-parent = <&ravenna_irq>;
        interrupt-names = "pp";
        interrupts = <8>;

        phy-handle = <&ra0_phy>;
        phy-mode = "rgmii";

        lawo,ptp-clock = <&ra_ptp0>;

        lawo,ptp-delay-path-rx-1000mbit-nsec = <0x2e0>;
        lawo,ptp-delay-path-rx-100mbit-nsec = <0x68>;
        lawo,ptp-delay-path-rx-10mbit-nsec = <0x1900>;
        lawo,ptp-delay-path-tx-nsec = <0x08>;
    };
```

## Stream device

This driver supports the stream interface, including the StreamTables and
TrackTables for RX and TX, as well as the RTCP statistics interface.

The driver exposes a character device in `/dev` with the name given in the
corresponding device-tree node which is used with `ioctl()` calls to configure
the device. Refer to the the UAPI header file `ravenna-stream-device.h` for
details.

### DebugFS entries

The driver exposes a debugfs interface under `/sys/kernel/debug/<device-name>/` with
the following entries:

* `info` shows information on the driver version and the character device.
For instance:
```
Driver version: 5ba23cc
Device name: ravenna-stream-device
Device minor: 124
```

* `rx/decoder`
```
RX decoder data dropped counter: 0
RX decoder FIFO overflow counter: 2
```
* `rx/summary`
```
Streams: 8/128
Track table entries: 64/1024
Tracks: 64/256
```

* `rx/streams`
```
Stream #0
  Created by: PID 1117
  Primary network
    Source: 192.168.100.2
    Destination: 238.228.114.84:5004
  Channels: 8
  Codec: 24-bit
  RTP payload type: 0
  Mode: SYNTONOUS 
  Track table start index: 8
  Channel -> Track mapping:
         0 ->   8     1 ->   9     2 ->  10     3 ->  11     4 ->  12     5 ->  13     6 ->  14     7 ->  15
```

* `rx/stream-table`
```
Entry #0 (VALID, ACTIVE)
  00000000: 53 72 e4 ee 53 72 e4 ee 00 00 8c 13 08 00 18 c0  Sr..Sr..........
  00000010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
```

* `rx/track-table`
```
           0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f
-------------------------------------------------------------------------------------------
  0x000 |    0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15 
  0x010 |   16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31 
  0x020 |   32   33   34   35   36   37   38   39   40   41   42   43   44   45   46   47 
  0x030 |   48   49   50   51   52   53   54   55   56   57   58   59   60   61   62   63 
  0x040 |   -    -    -    -    -    -    -    -    -    -    -    -    -    -    -    -  
  0x050 |   -    -    -    -    -    -    -    -    -    -    -    -    -    -    -    -  
```

Note that for each active stream, a range of consecutive tracks is allocated in the track table for all of its channels.
Channels that are not mapped to a track are shown as `M`. Unallocated tracks are marked with `-`.

* `rx/hash-table`
```
Hash table entries: 8
Large clusters: 0
Maximum cluster length: 0
Fragmented entries: 0
```

* `tx/summary`
```
Streams: 1/64
Track table entries: 8/1024
```

* `tx/streams`
```
Stream #0
  Created by: PID 1126
  Primary network
    Source: 192.168.100.5:1234
    Destination: 238.228.114.87:5004
    Destination MAC: 01:02:03:04:05:06
  Channels: 8
  Samples: 16
  Codec: 24-bit
  RTP payload type: 0
  Mode: MULTICAST 
  Track table start index: 32
  Channel -> Track mapping:
         0 ->  32     1 ->  33     2 ->  34     3 ->  35     4 ->  36     5 ->  37     6 ->  38     7 ->  39
...
```

* `tx/stream-table`
```
Entry #0 (VALID, ACTIVE)
  00000000: 00 00 18 c9 08 00 10 00 53 72 e4 ee 00 00 00 00  ........Sr......
  00000010: 04 03 02 01 00 00 06 05 00 00 00 00 00 00 00 00  ................
  00000020: a8 01 00 04 01 64 a8 c0 00 00 00 00 8c 13 d2 04  .....d..........
  00000030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
```

* `tx/track-table`
```
           0x00 0x01 0x02 0x03 0x04 0x05 0x06 0x07 0x08 0x09 0x0a 0x0b 0x0c 0x0d 0x0e 0x0f
-------------------------------------------------------------------------------------------
  0x000 |   -    -    -    -    -    -    -    -     8    9   10   11   12   13   14   15 
  0x010 |   16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31 
  0x020 |    0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15 
  0x030 |   16   17   18   19   20   21   22   23   24   25   26   27   28   29   30   31 
  0x040 |   -    -    -    -    -    -    -    -    -    -    -    -    -    -    -    -  
  0x050 |   -    -    -    -    -    -    -    -    -    -    -    -    -    -    -    -  
...
```

Note that for each active stream, a range of consecutive tracks is allocated in the track table for all of its channels.
Channels that are not mapped to a track are shown as `M`. Unallocated tracks are marked with `-`.

### DTS properties

| Property name                          | Mandatory | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-stream-device`        |
| `reg`                                  | *         | Register space                              |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                          |
| `lawo,device-name`                     | *         | Character device name to use                |
| `#lawo,tracks`                         | *         | Number of tracks supported by hardware      |
| `stream-table-tx`                      | *         | phandle to the TX stream table node         |
| `track-table-tx`                       | *         | phandle to the TX track table node          |
| `stream-table-rx`                      | *         | phandle to the RX stream table node         |
| `track-table-rx`                       | *         | phandle to the RX track table node          |

### Example DTS binding:

```
    ravenna-stream-device@a0181000 {
        compatible = "lawo,ravenna-stream-device";
        reg = <0x0 0xa0181000 0x0 0x1e00>;

        interrupt-parent = <&ravenna_irq>;
        interrupts = <16>;

        lawo,device-name = "ravenna-stream-device";
        #lawo,tracks = <256>;

        stream-table-tx = <&stream_table_tx_0>;
        track-table-tx = <&track_table_tx_0>;

        stream-table-rx = <&stream_table_rx_0>;
        track-table-rx = <&track_table_rx_0>;
    };

    stream_table_tx_0: stream-table-tx@a0004000 {
        compatible = "lawo,ravenna-stream-table-tx", "syscon";
        reg = <0x0 0xa0004000 0x0 0x1000>;
    };

    track_table_tx_0: track-table@a0007000 {
        compatible = "lawo,ravenna-track-table", "syscon";
        reg = <0x0 0xa0007000 0x0 0x1000>;
    };

    stream_table_rx_0: stream-table-rx@a0003000 {
        compatible = "lawo,ravenna-stream-table-rx", "syscon";
        reg = <0x0 0xa0003000 0x0 0x1000>;
    };

    track_table_rx_0: track-table@a0006000 {
        compatible = "lawo,ravenna-track-table", "syscon";
        reg = <0x0 0xa0006000 0x0 0x1000>;
    };
```

Note that there are no dedicated drivers for the table nodes, they are handled
by the generic `syscon` kernel core. The maximum entries for the tables are
inferred through the length of the register space.

## PTP

This driver supports the PTP interfaces.

### SysFS entries

`rtp_timestamp` returns the last reported PTP and RTP timestamp values in decimal format, separated by a whitespace.
The first value is the PTP timestamp in nanoseconds since the UNIX epoch.
The second value is the local RTP word clock counter.

### DTS properties

| Property name                          | Mandatory | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-ptp`                  |
| `reg`                                  | *         | Register space                              |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                          |
| `lawo,periodic-output-interval-ns`     |           | Periodic output interval, in nanoseconds    |

### Example DTS binding:

```
    ra_ptp0: ravenna-ptp@a0182e00 {
        compatible = "lawo,ravenna-ptp";
        reg = <0x0 0xa0182e00 0x0 0x200>;

        interrupt-parent = <&ravenna_irq>;
        interrupts = <30>;

	lawo,periodic-output-interval-ns = <312500>;
    };
```
