package kafka

import (
	"errors"
	"fmt"
	"testing"
	"time"
)

func TestRetryConfigBackoffUsesExponentialCap(t *testing.T) {
	t.Parallel()

	config := RetryConfig{
		InitialBackoff:    250 * time.Millisecond,
		MaxBackoff:        2 * time.Second,
		BackoffMultiplier: 2,
	}

	tests := []struct {
		attempt int
		want    time.Duration
	}{
		{attempt: 0, want: 250 * time.Millisecond},
		{attempt: 1, want: 250 * time.Millisecond},
		{attempt: 2, want: 500 * time.Millisecond},
		{attempt: 3, want: time.Second},
		{attempt: 4, want: 2 * time.Second},
		{attempt: 9, want: 2 * time.Second},
	}

	for _, tt := range tests {
		tt := tt
		t.Run(fmt.Sprintf("attempt_%d", tt.attempt), func(t *testing.T) {
			t.Parallel()

			if got := config.CalculateBackoff(tt.attempt); got != tt.want {
				t.Fatalf("CalculateBackoff(%d) = %v, want %v", tt.attempt, got, tt.want)
			}
		})
	}
}

func TestRetryableErrorClassification(t *testing.T) {
	t.Parallel()

	if !IsRetryableError(fmt.Errorf("wrapped: %w", ErrServiceUnavailable)) {
		t.Fatalf("wrapped service unavailable should be retryable")
	}
	if !IsRetryableError(errors.New("upstream returned TOO MANY REQUESTS")) {
		t.Fatalf("keyword retryable error should be retryable")
	}
	if IsRetryableError(ErrInvalidPayload) {
		t.Fatalf("invalid payload must not be retryable")
	}
	if IsRetryableError(nil) {
		t.Fatalf("nil error must not be retryable")
	}
}

func TestCategorizeErrorSeparatesRetryableNonRetryableAndUnknown(t *testing.T) {
	t.Parallel()

	if got := CategorizeError(ErrNetworkTimeout); got != ErrorCategoryRetryable {
		t.Fatalf("network timeout category = %s, want retryable", got)
	}
	if got := CategorizeError(fmt.Errorf("wrapped: %w", ErrInvalidObjectKey)); got != ErrorCategoryNonRetryable {
		t.Fatalf("invalid object key category = %s, want non_retryable", got)
	}
	if got := CategorizeError(errors.New("unexpected codec failure")); got != ErrorCategoryUnknown {
		t.Fatalf("unknown category = %s, want unknown", got)
	}
}
