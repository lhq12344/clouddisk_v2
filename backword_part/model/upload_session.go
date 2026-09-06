package model

import (
	"time"

	"gorm.io/gorm"
)

const (
	UploadSessionInitiated         = "initiated"
	UploadSessionUploading         = "uploading"
	UploadSessionCompleting        = "completing"
	UploadSessionPendingScan       = "pending_scan"
	UploadSessionReady             = "ready"
	UploadSessionInfected          = "infected"
	UploadSessionFailed            = "failed"
	UploadSessionAborted           = "aborted"
	UploadSessionReconcileRequired = "reconcile_required"
)

// UploadSession is the durable Core API source of truth for multipart uploads.
// Redis may cache this data, but it must not be the only recovery mechanism.
type UploadSession struct {
	gorm.Model
	UploadID        string     `gorm:"type:varchar(64);not null;uniqueIndex:uk_upload_id"`
	OwnerAccountID  uint       `gorm:"not null;index:idx_upload_owner"`
	FileName        string     `gorm:"type:varchar(255);not null"`
	FileHash        string     `gorm:"type:varchar(64);not null;index:idx_upload_hash"`
	FileSize        int64      `gorm:"type:bigint;not null"`
	ContentType     string     `gorm:"type:varchar(255)"`
	ObjectKey       string     `gorm:"type:varchar(512);not null;index:idx_upload_object"`
	StorageUploadID string     `gorm:"type:varchar(255);not null"`
	PartSize        int64      `gorm:"type:bigint;not null"`
	TotalParts      int        `gorm:"not null"`
	Status          string     `gorm:"type:varchar(32);not null;index:idx_upload_status"`
	IdempotencyKey  string     `gorm:"type:varchar(128);index:idx_upload_idempotency"`
	ExpiresAt       time.Time  `gorm:"not null;index:idx_upload_expires"`
	CompletedAt     *time.Time `gorm:"index"`
	LastError       string     `gorm:"type:text"`
}
