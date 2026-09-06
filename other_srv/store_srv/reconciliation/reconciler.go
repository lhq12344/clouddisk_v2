// Package reconciliation audits eventual-consistency boundaries owned by the
// storage worker. Its first iteration is deliberately read-only: operators
// receive an actionable report before any automated repair is introduced.
package reconciliation

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/minio/minio-go/v7"
	"gorm.io/gorm"
)

const (
	defaultInterval         = 5 * time.Minute
	defaultPendingScanAge   = 15 * time.Minute
	defaultObjectAuditLimit = 200
)

// Config bounds reconciliation work so a periodic audit cannot become a
// database or object-storage hot path.
type Config struct {
	Interval         time.Duration
	PendingScanAge   time.Duration
	ObjectAuditLimit int
}

// Report is logged as one structured record for each audit run. It is also a
// stable seam for exporting metrics later without changing audit semantics.
type Report struct {
	CheckedAt                   time.Time `json:"checked_at"`
	PendingScanOlderThanWindow  int64     `json:"pending_scan_older_than_window"`
	ExpiredIncompleteSessions   int64     `json:"expired_incomplete_sessions"`
	ReconcileRequiredSessions   int64     `json:"reconcile_required_sessions"`
	OutboxBacklog               int64     `json:"outbox_backlog"`
	ExpiredOutboxLeases         int64     `json:"expired_outbox_leases"`
	ExpiredInboxLeases          int64     `json:"expired_inbox_leases"`
	DLQMessages                 int64     `json:"dlq_messages"`
	RecentlyScannedFiles        int64     `json:"recently_scanned_files"`
	AverageRecentScanMillis     int64     `json:"average_recent_scan_millis"`
	MissingMetadataObjects      int64     `json:"missing_metadata_objects"`
	AuditedStorageObjects       int       `json:"audited_storage_objects"`
	OrphanStorageObjects        int       `json:"orphan_storage_objects"`
	StorageObjectAuditTruncated bool      `json:"storage_object_audit_truncated"`
}

// ObjectStore is the narrow storage seam reconciliation needs. Keeping this
// interface local avoids importing service globals and makes the audit package
// safe to unit-test without starting middleware clients.
type ObjectStore interface {
	StatObject(ctx context.Context, objectKey string) error
	ListObjectKeys(ctx context.Context, limit int) (keys []string, truncated bool, err error)
}

// Reconciler examines MySQL and MinIO without changing either. A nil MinIO
// client still permits all database checks and reports object checks as errors.
type Reconciler struct {
	db     *gorm.DB
	store  ObjectStore
	config Config
	now    func() time.Time
}

func DefaultConfig() Config {
	return Config{
		Interval:         durationFromEnv("RECONCILIATION_INTERVAL_SECONDS", defaultInterval),
		PendingScanAge:   durationFromEnv("RECONCILIATION_PENDING_SCAN_AGE_SECONDS", defaultPendingScanAge),
		ObjectAuditLimit: positiveIntFromEnv("RECONCILIATION_OBJECT_AUDIT_LIMIT", defaultObjectAuditLimit),
	}
}

func New(db *gorm.DB, store ObjectStore, config Config) *Reconciler {
	if config.Interval <= 0 {
		config.Interval = defaultInterval
	}
	if config.PendingScanAge <= 0 {
		config.PendingScanAge = defaultPendingScanAge
	}
	if config.ObjectAuditLimit <= 0 {
		config.ObjectAuditLimit = defaultObjectAuditLimit
	}
	return &Reconciler{db: db, store: store, config: config, now: time.Now}
}

// Start runs one audit at process start and then repeats until ctx is cancelled.
func (r *Reconciler) Start(ctx context.Context) {
	r.runAndLog(ctx)

	ticker := time.NewTicker(r.config.Interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			r.runAndLog(ctx)
		}
	}
}

func (r *Reconciler) runAndLog(ctx context.Context) {
	report, err := r.Run(ctx)
	if err != nil {
		log.Printf("reconciliation audit failed: %v", err)
		return
	}
	encoded, err := json.Marshal(report)
	if err != nil {
		log.Printf("reconciliation report marshal failed: %v", err)
		return
	}
	log.Printf("reconciliation audit report=%s", encoded)
}

// Run performs a bounded, read-only consistency audit.
func (r *Reconciler) Run(ctx context.Context) (Report, error) {
	if r == nil || r.db == nil {
		return Report{}, fmt.Errorf("reconciliation database is not initialized")
	}

	now := r.now()
	report := Report{CheckedAt: now}
	pendingCutoff := now.Add(-r.config.PendingScanAge)
	recentCutoff := now.Add(-r.config.Interval)

	checks := []struct {
		target *int64
		query  func() *gorm.DB
	}{
		{&report.PendingScanOlderThanWindow, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.File{}).
				Where("status = ? AND created_at < ?", model.FilePendingScan, pendingCutoff)
		}},
		{&report.ExpiredIncompleteSessions, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.UploadSession{}).
				Where("expires_at < ? AND status NOT IN ?", now, []string{
					model.UploadSessionReady, model.UploadSessionInfected,
					model.UploadSessionFailed, model.UploadSessionAborted,
				})
		}},
		{&report.ReconcileRequiredSessions, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.UploadSession{}).
				Where("status = ?", model.UploadSessionReconcileRequired)
		}},
		{&report.OutboxBacklog, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.Outbox{}).
				Where("status IN ?", []model.OutboxStatus{model.OutboxNew, model.OutboxFailed})
		}},
		{&report.ExpiredOutboxLeases, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.Outbox{}).
				Where("status = ? AND locked_until IS NOT NULL AND locked_until < ?", model.OutboxSending, now)
		}},
		{&report.ExpiredInboxLeases, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.Inbox{}).
				Where("status = ? AND locked_until IS NOT NULL AND locked_until < ?", model.InboxProcessing, now)
		}},
		{&report.DLQMessages, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.DLQFailure{})
		}},
		{&report.RecentlyScannedFiles, func() *gorm.DB {
			return r.db.WithContext(ctx).Model(&model.File{}).
				Where("scanned_at IS NOT NULL AND scanned_at >= ?", recentCutoff)
		}},
	}
	for _, check := range checks {
		if err := check.query().Count(check.target).Error; err != nil {
			return report, err
		}
	}

	if err := r.averageRecentScanMillis(ctx, recentCutoff, &report.AverageRecentScanMillis); err != nil {
		return report, err
	}
	if err := r.auditMetadataObjects(ctx, &report); err != nil {
		return report, err
	}
	if err := r.auditStorageObjects(ctx, &report); err != nil {
		return report, err
	}
	return report, nil
}

func (r *Reconciler) averageRecentScanMillis(ctx context.Context, recentCutoff time.Time, target *int64) error {
	var average *float64
	err := r.db.WithContext(ctx).Model(&model.File{}).
		Select("AVG(TIMESTAMPDIFF(MICROSECOND, created_at, scanned_at) / 1000)").
		Where("scanned_at IS NOT NULL AND scanned_at >= ?", recentCutoff).
		Scan(&average).Error
	if err != nil {
		return err
	}
	if average != nil && *average > 0 {
		*target = int64(*average)
	}
	return nil
}

func (r *Reconciler) auditMetadataObjects(ctx context.Context, report *Report) error {
	if r.store == nil {
		return fmt.Errorf("reconciliation object store is not initialized")
	}

	var files []model.File
	if err := r.db.WithContext(ctx).
		Where("object_key <> '' AND status IN ?", []string{model.FilePendingScan, model.FileSuccess}).
		Order("id ASC").
		Limit(r.config.ObjectAuditLimit).
		Find(&files).Error; err != nil {
		return err
	}
	for _, file := range files {
		err := r.store.StatObject(ctx, file.ObjectKey)
		if err == nil {
			continue
		}
		response := minio.ToErrorResponse(err)
		if response.Code == "NoSuchKey" || response.Code == "NotFound" {
			report.MissingMetadataObjects++
			continue
		}
		return fmt.Errorf("stat metadata object %q: %w", file.ObjectKey, err)
	}
	return nil
}

func (r *Reconciler) auditStorageObjects(ctx context.Context, report *Report) error {
	if r.store == nil {
		return fmt.Errorf("reconciliation object store is not initialized")
	}

	objectKeys, truncated, err := r.store.ListObjectKeys(ctx, r.config.ObjectAuditLimit)
	if err != nil {
		return fmt.Errorf("list storage objects: %w", err)
	}
	report.StorageObjectAuditTruncated = truncated
	report.AuditedStorageObjects = len(objectKeys)
	if len(objectKeys) == 0 {
		return nil
	}

	var known []string
	if err := r.db.WithContext(ctx).Model(&model.File{}).
		Where("object_key IN ?", objectKeys).
		Pluck("object_key", &known).Error; err != nil {
		return err
	}
	knownSet := make(map[string]struct{}, len(known))
	for _, key := range known {
		knownSet[key] = struct{}{}
	}
	for _, key := range objectKeys {
		if _, exists := knownSet[key]; !exists {
			report.OrphanStorageObjects++
		}
	}
	return nil
}

func durationFromEnv(name string, fallback time.Duration) time.Duration {
	raw := strings.TrimSpace(os.Getenv(name))
	seconds, err := strconv.Atoi(raw)
	if err != nil || seconds <= 0 {
		return fallback
	}
	return time.Duration(seconds) * time.Second
}

func positiveIntFromEnv(name string, fallback int) int {
	raw := strings.TrimSpace(os.Getenv(name))
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return fallback
	}
	return value
}
