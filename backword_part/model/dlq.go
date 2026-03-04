package model

import (
	"time"
)

// DLQStatus DLQ 消息状态
type DLQStatus string

const (
	DLQStatusPending  DLQStatus = "pending"  // 待处理
	DLQStatusRetrying DLQStatus = "retrying" // 重试中
	DLQStatusFailed   DLQStatus = "failed"   // 最终失败
	DLQStatusResolved DLQStatus = "resolved" // 已解决
)

// DLQFailure 死信队列失败记录表
type DLQFailure struct {
	ID        uint      `gorm:"primaryKey;autoIncrement"`
	Topic     string    `gorm:"type:varchar(255);not null;index:idx_topic_offset"`
	Partition int32     `gorm:"not null;index:idx_topic_offset"`
	Offset    int64     `gorm:"not null;index:idx_topic_offset"`

	// 业务字段
	UserID    string    `gorm:"type:varchar(64);index:idx_user_id"`
	ObjectKey string    `gorm:"type:varchar(512)"`
	FileHash  string    `gorm:"type:varchar(128)"`
	EventID   string    `gorm:"type:varchar(64);index:idx_event_id"`

	// 原始消息
	OriginalKey   string    `gorm:"type:varchar(255)"`
	OriginalValue []byte    `gorm:"type:mediumblob"` // 原始消息内容

	// 失败信息
	FailureReason   string    `gorm:"type:text"`
	FailureCount    int       `gorm:"not null;default:1"`
	ErrorCategory   string    `gorm:"type:varchar(32);index:idx_category"` // retryable/non_retryable/unknown
	ErrorStack      string    `gorm:"type:text"`
	FirstFailedAt   time.Time `gorm:"not null;index:idx_first_failed"`
	LastFailedAt    time.Time `gorm:"not null;index:idx_last_failed"`

	// 状态管理
	Status     DLQStatus `gorm:"type:varchar(16);not null;default:'pending';index:idx_status_last_failed"`
	ResolvedAt *time.Time
	ResolvedBy string    `gorm:"type:varchar(128)"` // 解决人/系统
	Resolution string    `gorm:"type:text"`         // 解决方案说明

	CreatedAt time.Time `gorm:"autoCreateTime"`
	UpdatedAt time.Time `gorm:"autoUpdateTime"`
}

// TableName 指定表名
func (DLQFailure) TableName() string {
	return "dlq_failures"
}

// DLQMetrics DLQ 监控指标（内存结构，不持久化）
type DLQMetrics struct {
	// 主队列指标
	MainQueueLag   int64   `json:"main_queue_lag"`
	ProcessingRate float64 `json:"processing_rate"` // msg/s
	ErrorRate      float64 `json:"error_rate"`      // 错误率

	// DLQ 指标
	DLQMessageCount  int64   `json:"dlq_message_count"`
	DLQGrowthRate    float64 `json:"dlq_growth_rate"`    // msg/min
	DLQRecoveryRate  float64 `json:"dlq_recovery_rate"`  // msg/min
	DLQPendingCount  int64   `json:"dlq_pending_count"`
	DLQRetryingCount int64   `json:"dlq_retrying_count"`
	DLQFailedCount   int64   `json:"dlq_failed_count"`
	DLQResolvedCount int64   `json:"dlq_resolved_count"`

	// 按错误类型统计
	RetryableErrors    int64 `json:"retryable_errors"`
	NonRetryableErrors int64 `json:"non_retryable_errors"`
	UnknownErrors      int64 `json:"unknown_errors"`

	LastUpdated time.Time `json:"last_updated"`
}
