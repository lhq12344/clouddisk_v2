package internal

import (
	"os"
	"path/filepath"
	"strings"
	"time"
)

type ClamAVConfig struct {
	Host              string `mapstructure:"host"`
	Port              string `mapstructure:"port"`
	Socket            string `mapstructure:"socket"`
	TimeoutSeconds    int    `mapstructure:"timeout"`
	MaxScanSizeMB     int64  `mapstructure:"max_scan_size_mb"`
	MaxFileSizeMB     int64  `mapstructure:"max_file_size_mb"`
	StreamMaxLengthMB int64  `mapstructure:"stream_max_length_mb"`
}

func (c ClamAVConfig) Timeout() time.Duration {
	if c.TimeoutSeconds <= 0 {
		return 30 * time.Second
	}
	return time.Duration(c.TimeoutSeconds) * time.Second
}

func (c ClamAVConfig) SocketPath() string {
	if c.Socket != "" {
		return c.Socket
	}

	candidates := make([]string, 0, 6)
	if envSocket := strings.TrimSpace(os.Getenv("CLAMAV_SOCKET")); envSocket != "" {
		candidates = append(candidates, envSocket)
	}
	if wd, err := os.Getwd(); err == nil {
		candidates = append(candidates, filepath.Join(wd, "run", "clamav", "clamd-local.ctl"))
	}
	if exePath, err := os.Executable(); err == nil {
		exeDir := filepath.Dir(exePath)
		candidates = append(candidates,
			filepath.Join(exeDir, "run", "clamav", "clamd-local.ctl"),
			filepath.Join(exeDir, "..", "run", "clamav", "clamd-local.ctl"),
		)
	}
	candidates = append(candidates,
		"/run/clamav/clamd.ctl",
		"/var/run/clamav/clamd.ctl",
	)

	seen := make(map[string]struct{}, len(candidates))
	for _, path := range candidates {
		if path == "" {
			continue
		}
		path = filepath.Clean(path)
		if _, ok := seen[path]; ok {
			continue
		}
		seen[path] = struct{}{}
		if _, err := os.Stat(path); err == nil {
			return path
		}
	}
	return ""
}

func (c ClamAVConfig) EffectiveStreamLimitBytes() int64 {
	limits := []int64{
		mbToBytes(c.MaxScanSizeMB),
		mbToBytes(c.MaxFileSizeMB),
		mbToBytes(c.StreamMaxLengthMB),
	}

	var min int64
	for _, limit := range limits {
		if limit <= 0 {
			continue
		}
		if min == 0 || limit < min {
			min = limit
		}
	}
	return min
}

func mbToBytes(v int64) int64 {
	if v <= 0 {
		return 0
	}
	return v * 1024 * 1024
}
