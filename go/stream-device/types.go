package ravenna_stream_device

const (
	StreamCodecAM824 = 0
	StreamCodecL32   = 1
	StreamCodecL24   = 2
	StreamCodecL16   = 3
)

type Codec uint8

func (c Codec) String() string {
	switch c {
	case StreamCodecAM824:
		return "AM824"
	case StreamCodecL32:
		return "L32"
	case StreamCodecL24:
		return "L24"
	case StreamCodecL16:
		return "L16"
	default:
		panic("Unsupported Codec type")
	}
}

const (
	MaxEthernetPacketSize = 1460
	MaxChannels           = 256
	MaxTracks             = 256
	TrackNull             = int16(-1)
)
