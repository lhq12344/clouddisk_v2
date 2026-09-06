package internal

import "testing"

func TestRunningGoTestBinaryNameSupportsWindowsAndUnix(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name string
		path string
		want bool
	}{
		{"unix test binary", "/tmp/account.test", true},
		{"windows test binary", `C:\Users\lhq\AppData\Local\Temp\account.test.exe`, true},
		{"ordinary windows executable", `D:\Code\project\clouddisk_v2\bin\storage_control.exe`, false},
		{"ordinary unix executable", "/usr/local/bin/storage_control", false},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if got := runningGoTestBinaryName(tt.path); got != tt.want {
				t.Fatalf("runningGoTestBinaryName(%q) = %v, want %v", tt.path, got, tt.want)
			}
		})
	}
}

func TestEnvBoolOrDefault(t *testing.T) {
	t.Setenv("CLOUDDISK_BOOL_TRUE", "true")
	t.Setenv("CLOUDDISK_BOOL_FALSE", "false")
	t.Setenv("CLOUDDISK_BOOL_INVALID", "not-a-bool")

	if !envBoolOrDefault("CLOUDDISK_BOOL_TRUE", false) {
		t.Fatalf("true env value should parse to true")
	}
	if envBoolOrDefault("CLOUDDISK_BOOL_FALSE", true) {
		t.Fatalf("false env value should parse to false")
	}
	if !envBoolOrDefault("CLOUDDISK_BOOL_INVALID", true) {
		t.Fatalf("invalid env value should fall back to default")
	}
	if envBoolOrDefault("CLOUDDISK_BOOL_MISSING", false) {
		t.Fatalf("missing env value should fall back to default")
	}
}
