package storagecontrolpb

import (
	"context"
	"strings"
	"testing"
	"time"

	"go_test/internal"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

func TestRequireObjectKey(t *testing.T) {
	t.Parallel()

	got, err := requireObjectKey("  files/hash-value  ")
	if err != nil {
		t.Fatalf("requireObjectKey returned error: %v", err)
	}
	if got != "files/hash-value" {
		t.Fatalf("requireObjectKey trim mismatch: got %q", got)
	}

	if _, err := requireObjectKey(" \t\n "); status.Code(err) != codes.InvalidArgument {
		t.Fatalf("empty object key code mismatch: got %v err=%v", status.Code(err), err)
	}
}

func TestDurationOrDefault(t *testing.T) {
	t.Parallel()

	fallback := 15 * time.Minute
	if got := durationOrDefault(0, fallback); got != fallback {
		t.Fatalf("zero seconds should use fallback: got %v", got)
	}
	if got := durationOrDefault(-1, fallback); got != fallback {
		t.Fatalf("negative seconds should use fallback: got %v", got)
	}
	if got := durationOrDefault(42, fallback); got != 42*time.Second {
		t.Fatalf("positive seconds mismatch: got %v", got)
	}
}

func TestDetectMimeByName(t *testing.T) {
	t.Parallel()

	if got := detectMimeByName("report.pdf", "application/custom"); got != "application/custom" {
		t.Fatalf("fallback content type should win: got %q", got)
	}
	if got := detectMimeByName("photo.png", ""); got != "image/png" {
		t.Fatalf("png content type mismatch: got %q", got)
	}
	if got := detectMimeByName("no-extension", ""); got != "application/octet-stream" {
		t.Fatalf("unknown content type mismatch: got %q", got)
	}
}

func TestContentDisposition(t *testing.T) {
	t.Parallel()

	attachment := contentDisposition("hello world.txt", false)
	if !strings.HasPrefix(attachment, "attachment;") || !strings.Contains(attachment, "hello%20world.txt") {
		t.Fatalf("attachment disposition mismatch: %q", attachment)
	}

	inline := contentDisposition("预览.pdf", true)
	if !strings.HasPrefix(inline, "inline;") || !strings.Contains(inline, "filename*=UTF-8''") {
		t.Fatalf("inline disposition mismatch: %q", inline)
	}

	if got := contentDisposition("", false); got != "attachment" {
		t.Fatalf("empty filename disposition mismatch: got %q", got)
	}
}

func TestStorageControlRejectsNilRequests(t *testing.T) {
	t.Parallel()

	service := &StorageControlService{}
	ctx := context.Background()

	checks := []struct {
		name string
		call func() error
	}{
		{"InitiateMultipart", func() error { _, err := service.InitiateMultipart(ctx, nil); return err }},
		{"PresignPart", func() error { _, err := service.PresignPart(ctx, nil); return err }},
		{"ListParts", func() error { _, err := service.ListParts(ctx, nil); return err }},
		{"CompleteMultipart", func() error { _, err := service.CompleteMultipart(ctx, nil); return err }},
		{"AbortMultipart", func() error { _, err := service.AbortMultipart(ctx, nil); return err }},
		{"PresignGet", func() error { _, err := service.PresignGet(ctx, nil); return err }},
		{"HeadObject", func() error { _, err := service.HeadObject(ctx, nil); return err }},
	}

	for _, check := range checks {
		check := check
		t.Run(check.name, func(t *testing.T) {
			t.Parallel()
			if err := check.call(); status.Code(err) != codes.InvalidArgument {
				t.Fatalf("%s nil request code mismatch: got %v err=%v", check.name, status.Code(err), err)
			}
		})
	}
}

func TestStorageControlRequiresMinIOClient(t *testing.T) {
	old := internal.MinIOClient
	internal.MinIOClient = nil
	t.Cleanup(func() { internal.MinIOClient = old })

	service := &StorageControlService{}
	_, err := service.PresignGet(context.Background(), &PresignGetReq{ObjectKey: "files/hash", Filename: "file.txt"})
	if status.Code(err) != codes.Unavailable {
		t.Fatalf("expected unavailable when MinIO client is missing: got %v err=%v", status.Code(err), err)
	}
}
