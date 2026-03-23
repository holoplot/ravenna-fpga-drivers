package logger

import (
	"log/slog"
	"os"

	"github.com/lmittmann/tint"
)

func Setup(debug bool) {
	level := slog.LevelInfo
	if debug {
		level = slog.LevelDebug
	}

	handler := tint.NewHandler(os.Stderr, &tint.Options{
		Level:      level,
		TimeFormat: "15:04:05.000000",
		NoColor:    os.Getenv("NO_COLOR") != "",
		AddSource:  true,
	})

	slog.SetDefault(slog.New(handler))
}
