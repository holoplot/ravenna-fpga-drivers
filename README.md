# Kernel driver modules for LAWO's Ravenna FPGA implementation

## IRQ

This driver supports the IRQ mux. It supports up to 32 downstream IRQs.

### DTS properties

| Property name                          | Mandatory | Description                                |
|----------------------------------------|:---------:|--------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-irq-controller`      |
| `interrupt-controller`                 | *         |                                            |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                         |
| `#interrupt-cells`                     | *         | Set to `<1>`                               |
| `#address-cells`                       | *         | Set to `<1>`                               |
| `reg`                                  | *         | Register space                             |

### Example DTS binding

```
    ravenna_irq: ravenna-irq@a01ffc00 {
        compatible = "lawo,ravenna-irq-controller";

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

### SysFS entries

| Entry name				 | Access    | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `udp_throttled_packets`                | R/O       | `RA_NET_PP_CNT_UDP_THROTTLE`                |
| `rx_packets_parsed`                    | R/O       | `RA_NET_PP_CNT_RX_PARSED`                   |
| `rx_queue_errors`                      | R/O       | `RA_NET_PP_CNT_RX_QUEUE_ERR`                |
| `rx_checksum_errors`                   | R/O       | `RA_NET_PP_CNT_RX_IP_CHK_ERR`               |
| `rx_stream_packets_dropped`            | R/O       | `RA_NET_PP_CNT_RX_STREAM_DROP`              |
| `rx_stream_packets`                    | R/O       | `RA_NET_PP_CNT_RX_STREAM`                   |
| `rx_legacy_packets`                    | R/O       | `RA_NET_PP_CNT_RX_LEGACY`                   |
| `tx_stream_packets`                    | R/O       | `RA_NET_PP_CNT_TX_STREAM`                   |
| `tx_legacy_packets`                    | R/O       | `RA_NET_PP_CNT_TX_LEGACY`                   |
| `tx_stream_packets_lost`               | R/O       | `RA_NET_PP_CNT_TX_STREAM_LOST`              |
| `rtp_global_offset`                    | R/W       | `RA_NET_RTP_GLOBAL_OFFSET`                  |
| `counter_reset`                        | W/O       | `RA_NET_PP_CNT_RST`                         |

### DTS properties

| Property name                          | Mandatory | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-ethernet`             |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                          |
| `reg`                                  | *         | Register space                              |
| `phy-handle`                           | *         | PHY handle to use                           |
| `phy-mode`                             |           | PHY mode to set                             |
| `lawo-ptp-clock`                       |           | phandle to the Ravenna PTP clock node       |
| `lawo,ptp-delay-path-rx-1000mbit-nsec` |           | RX path delay in 1000 Mbit/s mode, in nsecs |
| `lawo,ptp-delay-path-rx-100mbit-nsec`  |           | RX path delay in 100 Mbit/s mode, in nsecs  |
| `lawo,ptp-delay-path-rx-10mbit-nsec`   |           | RX path delay in 10 Mbit/s mode, in nsecs   |
| `lawo,ptp-delay-path-tx-nsec`          |           | TX path delay for all modes, in nsecs       |

### Example DTS binding:

```
    ra0: ravenna-net@a0180000 {
        compatible = "lawo,ravenna-ethernet";
        reg = <0x0 0xa0180000 0x0 0x1000>;

        interrupt-parent = <&ravenna_irq>;
        interrupts = <8>;

        phy-handle = <&ra0_phy>;
        phy-mode = "rgmii";

        lawo,ptp-clock = <&ra_ptp0>;

        lawo,ptp-delay-path-rx-1000mbit-nsec = <0x6e>;
        lawo,ptp-delay-path-rx-100mbit-nsec = <0x2e>;
        lawo,ptp-delay-path-rx-10mbit-nsec = <0x8>;
        lawo,ptp-delay-path-tx-nsec = <0x1900>;
    };
```

## Stream device

This driver supports the stream interface, including the StreamTables and
TrackTables for RX and TX, as well as the RTCP statistics interface.

The driver exposes a chardev in `/dev` which bears the name given in the
corresponding device-tree node.

### DebugFS entries

* `info` shows information on the driver version and the chanacter device.
For instance:
```
Driver version: 5ba23cc
Device name: ravenna-stream-device
Device minor: 124
```

* `decoder`
```
RX decoder data dropped counter: 0
RX decoder fifo overflow counter: 2
```
* `rx-summary`
```
Streams: 8/128
Tracks: 64/1024
```

* `rx-streams`
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
  Track table entry: 8
  Channel -> Track association:
         0 ->   8     1 ->   9     2 ->  10     3 ->  11     4 ->  12     5 ->  13     6 ->  14     7 ->  15
```

* `rx-stream-table`
```
Entry #0 (VALID, ACTIVE)
  00000000: 53 72 e4 ee 53 72 e4 ee 00 00 8c 13 08 00 18 c0  Sr..Sr..........
  00000010: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
```

* `rx-track-table`
```
           0x00  0x01  0x02  0x03  0x04  0x05  0x06  0x07  0x08  0x09  0x0a  0x0b  0x0c  0x0d  0x0e  0x0f
---------------------------------------------------------------------------------------------------------
  0x000 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
  0x010 |  0x10  0x11  0x12  0x13  0x14  0x15  0x16  0x17  0x18  0x19  0x1a  0x1b  0x1c  0x1d  0x1e  0x1f 
  0x020 |  0x20  0x21  0x22  0x23  0x24  0x25  0x26  0x27  0x28  0x29  0x2a  0x2b  0x2c  0x2d  0x2e  0x2f 
  0x030 |  0x30  0x31  0x32  0x33  0x34  0x35  0x36  0x37  0x38  0x39  0x3a  0x3b  0x3c  0x3d  0x3e  0x3f 
  0x040 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
  0x050 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
...
```

* `rx-hash-table`
```
Hash table entries: 8
Large clusters: 0
Maximum cluster length: 0
Fragmented entries: 0
```

* `tx-summary`
```
Streams: 1/64
Tracks: 8/1024
```

* `tx-streams`
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
  Track table entry: 32
  Channel -> Track association:
         0 ->  32     1 ->  33     2 ->  34     3 ->  35     4 ->  36     5 ->  37     6 ->  38     7 ->  39
...
```

* `tx-stream-table`
```
Entry #0 (VALID, ACTIVE)
  00000000: 00 00 18 c9 08 00 10 00 53 72 e4 ee 00 00 00 00  ........Sr......
  00000010: 04 03 02 01 00 00 06 05 00 00 00 00 00 00 00 00  ................
  00000020: a8 01 00 04 01 64 a8 c0 00 00 00 00 8c 13 d2 04  .....d..........
  00000030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
```

* `tx-track-table`
```
           0x00  0x01  0x02  0x03  0x04  0x05  0x06  0x07  0x08  0x09  0x0a  0x0b  0x0c  0x0d  0x0e  0x0f
---------------------------------------------------------------------------------------------------------
  0x000 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
  0x010 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
  0x020 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
  0x030 |  0x10  0x11  0x12  0x13  0x14  0x15  0x16  0x17  0x18  0x19  0x1a  0x1b  0x1c  0x1d  0x1e  0x1f 
  0x040 |   -     -     -     -     -     -     -     -     -     -     -     -     -     -     -     -   
...
```

### DTS properties

| Property name                          | Mandatory | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-stream-device`        |
| `reg`                                  | *         | Register space                              |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                          |
| `laow,device-name`                     | *         | Character device name to use                |
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

	laow,device-name = "ravenna-stream-device";

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

Note that there are no drivers for the table nodes, they are handled by the
generic `syscon` kernel core. The maximum entries for the tables are inferred
through the length of the register space.

## PTP

This driver supports the PTP interfaces.

### SysFS entries

`rtp_timestamp` ...

### DTS properties

| Property name                          | Mandatory | Description                                 |
|----------------------------------------|:---------:|---------------------------------------------|
| `compatible`                           | *         | Must be `lawo,ravenna-ptp`                  |
| `reg`                                  | *         | Register space                              |
| `interrupts`, `interrupt-parent`       | *         | Upstream interrupt                          |

### Example DTS binding:

```
    ra_ptp0: ravenna-ptp@a0182e00 {
        compatible = "lawo,ravenna-ptp";
        reg = <0x0 0xa0182e00 0x0 0x200>;

        interrupt-parent = <&ravenna_irq>;
        interrupts = <30>;
    };
```
