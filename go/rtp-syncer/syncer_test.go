package syncer

import (
	"math/big"
	"testing"
	"time"
)

func Test_subSamples_diff(t *testing.T) {
	tests := []struct {
		name string
		a, b subSamples
		want int64
	}{
		{
			name: "null",
			a:    0,
			b:    0,
			want: 0,
		},
		{
			name: "test1",
			a:    4003,
			b:    0,
			want: 4003,
		},
		{
			name: "test2",
			a:    1000,
			b:    1000,
			want: 0,
		},
		{
			name: "test3",
			a:    4003,
			b:    4004,
			want: -1,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.a.diff(tt.b); got != tt.want {
				t.Errorf("subSamples.diff() = %v, want %v", got, tt.want)
			}
		})
	}
}

func Test_subSamples_toOffset(t *testing.T) {
	tests := []struct {
		name string
		subs subSamples
		want uint32
	}{
		{
			name: "a",
			subs: 250 * subSamplesPerSample,
			want: 250,
		},
		{
			name: "b",
			subs: 250*subSamplesPerSample + 1,
			want: 250,
		},
		{
			name: "c",
			subs: 250*subSamplesPerSample + 2,
			want: 250,
		},
		{
			name: "d",
			subs: 250*subSamplesPerSample + 3,
			want: 250,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.subs.toSamples32(); got != tt.want {
				t.Errorf("subSamples.toSamples() = %v, want %v", got, tt.want)
			}
		})
	}
}

func TestRtpSyncer_ptpTimestampTosubSamples(t *testing.T) {
	tests := []struct {
		name         string
		sampleRate   int
		ptpTimestamp uint64
		want         subSamples
	}{
		{
			name:         "null",
			sampleRate:   48000,
			ptpTimestamp: 0,
			want:         0,
		},
		{
			name:         "test1",
			sampleRate:   48000,
			ptpTimestamp: uint64(time.Second.Nanoseconds()) + 20833,             // time of 1 sample at 48000 Hz
			want:         48000*subSamplesPerSample + (subSamplesPerSample - 1), // 48000 samples * 4 + 3 sub samples
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			s := &RtpSyncer{
				sampleRate: big.NewInt(int64(tt.sampleRate)),
			}
			if got := s.ptpTimestampToSubSamples(tt.ptpTimestamp); got != tt.want {
				t.Errorf("RtpSyncer.ptpTimestampTosubSamples() = %v, want %v", got, tt.want)
			}
		})
	}
}

func Test_subSamples_subtractSamples(t *testing.T) {
	tests := []struct {
		name string
		s    subSamples
		i    uint32
		want subSamples
	}{
		{
			name: "null",
			s:    0,
			i:    0,
			want: 0,
		},
		{
			name: "test1",
			s:    4003,
			i:    0,
			want: 4003,
		},
		{
			name: "test2",
			s:    4003,
			i:    1,
			want: 4003 - subSamplesPerSample,
		},
		{
			name: "overflow",
			s:    100000000000,
			i:    1073761061,
			want: 91409911512,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := tt.s.subtractSamples(tt.i); got != tt.want {
				t.Errorf("subSamples.subtractSamples() = %v, want %v", got, tt.want)
			}
		})
	}
}
