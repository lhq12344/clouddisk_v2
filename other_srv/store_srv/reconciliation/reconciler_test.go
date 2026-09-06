package reconciliation

import (
	"os"
	"testing"
	"time"
)

func TestDefaultConfigUsesSafeDefaults(t *testing.T) {
	t.Setenv("RECONCILIATION_INTERVAL_SECONDS", "")
	t.Setenv("RECONCILIATION_PENDING_SCAN_AGE_SECONDS", "")
	t.Setenv("RECONCILIATION_OBJECT_AUDIT_LIMIT", "")

	config := DefaultConfig()
	if config.Interval != defaultInterval {
		t.Fatalf("interval = %v, want %v", config.Interval, defaultInterval)
	}
	if config.PendingScanAge != defaultPendingScanAge {
		t.Fatalf("pending scan age = %v, want %v", config.PendingScanAge, defaultPendingScanAge)
	}
	if config.ObjectAuditLimit != defaultObjectAuditLimit {
		t.Fatalf("object audit limit = %d, want %d", config.ObjectAuditLimit, defaultObjectAuditLimit)
	}
}

func TestDefaultConfigHonorsPositiveOverrides(t *testing.T) {
	t.Setenv("RECONCILIATION_INTERVAL_SECONDS", "30")
	t.Setenv("RECONCILIATION_PENDING_SCAN_AGE_SECONDS", "90")
	t.Setenv("RECONCILIATION_OBJECT_AUDIT_LIMIT", "17")

	config := DefaultConfig()
	if config.Interval != 30*time.Second {
		t.Fatalf("interval = %v, want 30s", config.Interval)
	}
	if config.PendingScanAge != 90*time.Second {
		t.Fatalf("pending scan age = %v, want 90s", config.PendingScanAge)
	}
	if config.ObjectAuditLimit != 17 {
		t.Fatalf("object audit limit = %d, want 17", config.ObjectAuditLimit)
	}
}

func TestInvalidEnvironmentOverrideFallsBack(t *testing.T) {
	t.Setenv("RECONCILIATION_INTERVAL_SECONDS", "-1")
	t.Setenv("RECONCILIATION_OBJECT_AUDIT_LIMIT", "not-a-number")
	if got := durationFromEnv("RECONCILIATION_INTERVAL_SECONDS", time.Minute); got != time.Minute {
		t.Fatalf("duration fallback = %v, want 1m", got)
	}
	if got := positiveIntFromEnv("RECONCILIATION_OBJECT_AUDIT_LIMIT", 5); got != 5 {
		t.Fatalf("int fallback = %d, want 5", got)
	}
}

func TestEnvironmentHelpersDoNotRequireVariables(t *testing.T) {
	const name = "RECONCILIATION_TEST_UNSET"
	previous, existed := os.LookupEnv(name)
	os.Unsetenv(name)
	t.Cleanup(func() {
		if existed {
			os.Setenv(name, previous)
		}
	})
	if got := durationFromEnv(name, 2*time.Minute); got != 2*time.Minute {
		t.Fatalf("duration = %v, want 2m", got)
	}
}
