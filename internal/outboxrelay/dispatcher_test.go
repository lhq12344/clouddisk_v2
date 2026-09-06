package outboxrelay

import (
	"context"
	"strings"
	"testing"
	"time"

	"go_test/backword_part/model"
)

func TestNewDispatcherUsesOperationalDefaults(t *testing.T) {
	dispatcher := NewDispatcher(nil, nil)

	if dispatcher.pollInterval != 2*time.Second {
		t.Fatalf("poll interval = %v, want 2s", dispatcher.pollInterval)
	}
	if dispatcher.batchSize != 50 {
		t.Fatalf("batch size = %d, want 50", dispatcher.batchSize)
	}
	if dispatcher.workerCount != 8 {
		t.Fatalf("worker count = %d, want 8", dispatcher.workerCount)
	}
	if dispatcher.lockTTL != 30*time.Second {
		t.Fatalf("lock TTL = %v, want 30s", dispatcher.lockTTL)
	}
	if dispatcher.instanceID == "" {
		t.Fatalf("instance ID must be populated for outbox lease ownership")
	}
}

func TestBackoffIsExponentialAndCapped(t *testing.T) {
	tests := []struct {
		name string
		try  int
		want time.Duration
	}{
		{"zero uses first retry delay", 0, time.Second},
		{"first failure", 1, 2 * time.Second},
		{"second failure", 2, 4 * time.Second},
		{"tenth failure below cap", 10, 5 * time.Minute},
		{"large retry count capped", 99, 5 * time.Minute},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := backoff(tt.try); got != tt.want {
				t.Fatalf("backoff(%d) = %v, want %v", tt.try, got, tt.want)
			}
		})
	}
}

func TestSendOneFailsClosedWhenProducerMissing(t *testing.T) {
	dispatcher := &Dispatcher{}
	err := dispatcher.sendOne(context.Background(), &model.Outbox{
		Topic:   "file.scan",
		Key:     "scan:test-upload",
		Payload: `{"event_id":"scan:test-upload"}`,
	})

	if err == nil {
		t.Fatalf("expected missing Kafka producer to fail")
	}
	if !strings.Contains(err.Error(), "kafka producer not initialized") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestParseHeadersFailsClosedOnMalformedJSON(t *testing.T) {
	_, err := parseHeaders(`{"request_id":`)
	if err == nil {
		t.Fatalf("expected malformed headers to fail")
	}
	if !strings.Contains(err.Error(), "invalid outbox headers JSON") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestParseHeadersRejectsEmptyKeys(t *testing.T) {
	_, err := parseHeaders(`{"":"missing-key"}`)
	if err == nil {
		t.Fatalf("expected empty header key to fail")
	}
	if !strings.Contains(err.Error(), "header key is empty") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestParseHeadersAllowsEmptyObject(t *testing.T) {
	headers, err := parseHeaders(`{}`)
	if err != nil {
		t.Fatalf("expected empty header object to pass: %v", err)
	}
	if len(headers) != 0 {
		t.Fatalf("headers len = %d, want 0", len(headers))
	}
}

func TestMarkSentFailsClosedWhenDBMissing(t *testing.T) {
	dispatcher := &Dispatcher{instanceID: "relay-test"}
	err := dispatcher.markSent(context.Background(), 1)
	if err == nil {
		t.Fatalf("expected missing DB to fail")
	}
	if !strings.Contains(err.Error(), "db is not initialized") {
		t.Fatalf("unexpected error: %v", err)
	}
}
