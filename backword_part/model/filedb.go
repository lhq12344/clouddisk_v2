package model

import (
	"time"

	"gorm.io/gorm"
)

const (
	FilePendingScan = "pending_scan"
	FileSuccess     = "success"
	FileInfected    = "infected"
	FileScanFailed  = "scan_failed"
)

type File struct {
	gorm.Model
	Sha1        string     `gorm:"type:varchar(64);not null;uniqueIndex"` // 历史字段名，当前实际保存 SHA-256 内容哈希
	Size        int64      `gorm:"type:bigint"`
	Status      string     `gorm:"type:varchar(32);index"`
	ObjectKey   string     `gorm:"type:varchar(512);index"`
	ContentType string     `gorm:"type:varchar(255)"`
	ScanDetail  string     `gorm:"type:text"`
	ScannedAt   *time.Time `gorm:"index"`
}

// UserFile 用户文件关系表（谁拥有这个文件）
type UserFile struct {
	gorm.Model
	AccountID uint   `gorm:"not null;index:idx_account_id;uniqueIndex:uk_account_file_name"`
	FileID    uint   `gorm:"not null;index:idx_file_id;uniqueIndex:uk_account_file_name"`
	Name      string `gorm:"type:varchar(255);not null;uniqueIndex:uk_account_file_name"`

	File    File    `gorm:"foreignKey:FileID"`
	Account Account `gorm:"foreignKey:AccountID"`
}

type OutboxStatus string

const (
	OutboxNew     OutboxStatus = "NEW"
	OutboxSending OutboxStatus = "SENDING"
	OutboxSent    OutboxStatus = "SENT"
	OutboxFailed  OutboxStatus = "FAILED"
)
const (
	LoadFile              = "LoadFile"
	DownLoadFile          = "DownLoadFile"
	FileScanRequested     = "FILE_SCAN_REQUESTED"
	ObjectDeleteRequested = "OBJECT_DELETE_REQUESTED"
	FileUploadEventTopic  = "file.upload.cmd"
)

type DownloadCmdPayload struct {
	TxID      string `json:"tx_id"`
	EventID   string `json:"event_id"`
	FileID    uint   `json:"file_id"`
	Sha1      string `json:"sha1"` // 历史命名，语义上为 SHA-256 内容哈希
	Size      int32  `json:"size"`
	OssKey    string `json:"oss_key"`
	Type      string `json:"type"`
	EventType string `json:"event_type"`
}
type FileEventPayload struct {
	TxID        string `json:"tx_id"`
	EventID     string `json:"event_id"`
	FileID      uint   `json:"file_id"`
	UserID      string `json:"user_id"`
	Sha1        string `json:"sha1"` // 历史命名，语义上为 SHA-256 内容哈希
	Size        int64  `json:"size"`
	OssKey      string `json:"oss_key"`
	ContentType string `json:"content_type"`
	EventType   string `json:"event_type"`
}
type Outbox struct {
	gorm.Model
	EventID     string       `gorm:"type:varchar(64);not null;uniqueIndex:uk_event_id;comment:事件唯一ID（幂等键）"`
	TxID        string       `gorm:"type:varchar(64);not null;index:idx_tx_id;comment:业务事务ID（用于串联上传流程）"`
	EventType   string       `gorm:"type:varchar(64);not null;index:idx_event_type;comment:事件类型（用于版本演进与路由）"`
	Topic       string       `gorm:"type:varchar(128);not null;index:idx_topic;comment:目标Kafka topic"`
	Key         string       `gorm:"type:varchar(128);index:idx_key;comment:Kafka message key（分区/同key有序）"`
	Payload     string       `gorm:"type:json;not null;comment:消息体（JSON/或改成BLOB存protobuf）"`
	Headers     string       `gorm:"type:json;not null;comment:可选头信息（traceparent等）"`
	Status      OutboxStatus `gorm:"type:varchar(16);not null;index:idx_status_next;comment:投递状态 NEW/SENT/FAILED"`
	RetryCount  int          `gorm:"not null;default:0;comment:发送失败重试次数"`
	NextRetryAt time.Time    `gorm:"index:idx_status_next;comment:下次重试时间（退避策略）"`
	LastError   string       `gorm:"type:text;comment:最近一次失败原因（排障）"`
	// 多实例抢占锁（强烈建议）
	LockedBy    string     `gorm:"type:varchar(64);index:idx_lock;comment:发送锁持有者"`
	LockedUntil *time.Time `gorm:"index:idx_lock;comment:发送锁租约到期时间"`
}

type InboxStatus string

const (
	InboxProcessing InboxStatus = "PROCESSING" // 处理中
	InboxDone       InboxStatus = "DONE"       // 已完成（幂等，跳过）
	InboxDLQ        InboxStatus = "DLQ"        // 已进入死信队列
)

type Inbox struct {
	EventID      string      `gorm:"type:varchar(64);primaryKey;comment:幂等键(来自outbox.event_id)"`
	Status       InboxStatus `gorm:"type:varchar(16);not null;index"`
	LockedBy     string      `gorm:"type:varchar(64);index"`
	LockedUntil  *time.Time  `gorm:"index"`
	Attempts     int         `gorm:"not null;default:0"`
	LastError    string      `gorm:"type:text"`
	DLQFailureID *uint       `gorm:"index;comment:关联的DLQ失败记录ID"` // 新增：关联 DLQ
	CreatedAt    time.Time
	UpdatedAt    time.Time
	Messages     string `gorm:"type:varchar(128);default:null"`
}
