package kafka

import (
	"errors"
	"testing"

	"github.com/IBM/sarama"
)

func TestBuildDLQMessagePreservesOriginalEventContext(t *testing.T) {
	t.Parallel()

	msg := &sarama.ConsumerMessage{
		Topic:     "file.scan",
		Partition: 3,
		Offset:    42,
		Key:       []byte("scan:upload-1"),
		Value:     []byte(`{"event_id":"scan:upload-1","user_id":"7","sha1":"abc123","oss_key":"files/abc123"}`),
	}

	dlq, err := buildDLQMessage(msg, ErrOSSConnectionTimeout, 4)
	if err != nil {
		t.Fatalf("buildDLQMessage returned error: %v", err)
	}

	if dlq.EventID != "scan:upload-1" || dlq.UserID != "7" || dlq.FileHash != "abc123" || dlq.ObjectKey != "files/abc123" {
		t.Fatalf("DLQ message lost event context: %#v", dlq)
	}
	if dlq.OriginalTopic != msg.Topic || dlq.OriginalPartition != msg.Partition || dlq.OriginalOffset != msg.Offset || dlq.OriginalKey != string(msg.Key) {
		t.Fatalf("DLQ message lost Kafka origin: %#v", dlq)
	}
	if dlq.FailureCount != 4 {
		t.Fatalf("failure count = %d, want 4", dlq.FailureCount)
	}
	if dlq.ErrorCategory != string(ErrorCategoryRetryable) {
		t.Fatalf("error category = %s, want retryable", dlq.ErrorCategory)
	}
	if dlq.FirstFailedAt.IsZero() || dlq.LastFailedAt.IsZero() {
		t.Fatalf("failure timestamps must be populated")
	}
}

func TestBuildDLQMessageRejectsInvalidOriginalPayload(t *testing.T) {
	t.Parallel()

	_, err := buildDLQMessage(&sarama.ConsumerMessage{Value: []byte(`not-json`)}, errors.New("boom"), 1)
	if err == nil {
		t.Fatalf("invalid original payload should be rejected")
	}
}
