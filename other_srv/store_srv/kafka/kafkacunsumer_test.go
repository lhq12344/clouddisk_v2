package kafka

import (
	"strings"
	"testing"
)

func TestNormalizeObjectKey(t *testing.T) {
	t.Parallel()

	if got := normalizeObjectKey("custom/key", "hash"); got != "custom/key" {
		t.Fatalf("explicit object key should win: got %q", got)
	}
	if got := normalizeObjectKey("", "hash"); got != "files/hash" {
		t.Fatalf("hash fallback object key mismatch: got %q", got)
	}
	if got := normalizeObjectKey("", ""); got != "" {
		t.Fatalf("empty key/hash should remain empty: got %q", got)
	}
}

func TestExtractEventID(t *testing.T) {
	t.Parallel()

	got, err := extractEventID([]byte(`{"event_id":"scan:upload-123","sha1":"abc"}`))
	if err != nil {
		t.Fatalf("extractEventID returned error: %v", err)
	}
	if got != "scan:upload-123" {
		t.Fatalf("event id mismatch: got %q", got)
	}

	got, err = extractEventID([]byte(`{"event_id":"  delete:file-7  "}`))
	if err != nil {
		t.Fatalf("extractEventID trim case returned error: %v", err)
	}
	if got != "delete:file-7" {
		t.Fatalf("trimmed event id mismatch: got %q", got)
	}

	if _, err := extractEventID([]byte(`not-json`)); err == nil {
		t.Fatalf("invalid JSON should return an error")
	}
}

func TestTruncateReason(t *testing.T) {
	t.Parallel()

	if got := truncateReason("  short reason  "); got != "short reason" {
		t.Fatalf("short reason trim mismatch: got %q", got)
	}

	longReason := strings.Repeat("x", 1100)
	got := truncateReason(longReason)
	if len(got) != 1024 {
		t.Fatalf("long reason length mismatch: got %d", len(got))
	}
	if got != strings.Repeat("x", 1024) {
		t.Fatalf("long reason was not truncated deterministically")
	}
}
