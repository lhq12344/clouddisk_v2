package kafka

import (
	"context"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"time"

	"go.uber.org/zap"
)

// DLQManager DLQ 管理服务
type DLQManager struct {
	logger    *zap.Logger
	processor *FileUploadConsumer
}

// NewDLQManager 创建 DLQ 管理器
func NewDLQManager(processor *FileUploadConsumer) *DLQManager {
	return &DLQManager{
		logger:    internal.Logger,
		processor: processor,
	}
}

// DLQFilter DLQ 查询过滤器
type DLQFilter struct {
	Status        *model.DLQStatus
	UserID        *string
	EventID       *string
	ErrorCategory *string
	StartTime     *time.Time
	EndTime       *time.Time
	Limit         int
	Offset        int
}

// ListDLQMessages 查询 DLQ 消息列表
func (m *DLQManager) ListDLQMessages(ctx context.Context, filter DLQFilter) ([]*model.DLQFailure, int64, error) {
	query := internal.DB.WithContext(ctx).Model(&model.DLQFailure{})

	// 应用过滤条件
	if filter.Status != nil {
		query = query.Where("status = ?", *filter.Status)
	}
	if filter.UserID != nil {
		query = query.Where("user_id = ?", *filter.UserID)
	}
	if filter.EventID != nil {
		query = query.Where("event_id = ?", *filter.EventID)
	}
	if filter.ErrorCategory != nil {
		query = query.Where("error_category = ?", *filter.ErrorCategory)
	}
	if filter.StartTime != nil {
		query = query.Where("first_failed_at >= ?", *filter.StartTime)
	}
	if filter.EndTime != nil {
		query = query.Where("first_failed_at <= ?", *filter.EndTime)
	}

	// 统计总数
	var total int64
	if err := query.Count(&total).Error; err != nil {
		return nil, 0, err
	}

	// 分页查询
	if filter.Limit <= 0 {
		filter.Limit = 20
	}
	if filter.Limit > 100 {
		filter.Limit = 100
	}

	var failures []*model.DLQFailure
	err := query.Order("first_failed_at DESC").
		Limit(filter.Limit).
		Offset(filter.Offset).
		Find(&failures).Error

	return failures, total, err
}

// GetDLQMessage 获取单个 DLQ 消息详情
func (m *DLQManager) GetDLQMessage(ctx context.Context, id uint) (*model.DLQFailure, error) {
	var failure model.DLQFailure
	err := internal.DB.WithContext(ctx).First(&failure, id).Error
	return &failure, err
}

// RetryMessage 手动重试单条消息
func (m *DLQManager) RetryMessage(ctx context.Context, id uint) error {
	// 查询消息
	var failure model.DLQFailure
	if err := internal.DB.WithContext(ctx).First(&failure, id).Error; err != nil {
		return fmt.Errorf("message not found: %w", err)
	}

	// 检查状态
	if failure.Status == model.DLQStatusResolved {
		return fmt.Errorf("message already resolved")
	}

	m.logger.Info("[DLQManager]Manually retrying message",
		zap.Uint("id", id),
		zap.String("event_id", failure.EventID))

	// 更新状态为 retrying
	if err := internal.DB.Model(&failure).Update("status", model.DLQStatusRetrying).Error; err != nil {
		return err
	}

	// 重新构造消息
	msg := &sarama.ConsumerMessage{
		Topic:     failure.Topic,
		Partition: failure.Partition,
		Offset:    failure.Offset,
		Key:       []byte(failure.OriginalKey),
		Value:     failure.OriginalValue,
	}

	// 处理消息
	err := m.processor.processMessage(ctx, msg)

	if err == nil {
		// 成功：标记为 resolved，同时更新 Inbox
		return internal.DB.Transaction(func(tx *gorm.DB) error {
			now := time.Now()

			// 更新 DLQ
			if err := tx.Model(&failure).Updates(map[string]interface{}{
				"status":      model.DLQStatusResolved,
				"resolved_at": &now,
				"resolved_by": "manual_retry",
				"resolution":  "Manually retried and succeeded",
			}).Error; err != nil {
				return err
			}

			// 更新 Inbox
			if failure.EventID != "" {
				if err := tx.Model(&model.Inbox{}).
					Where("event_id = ?", failure.EventID).
					Updates(map[string]interface{}{
						"status":     model.InboxDone,
						"last_error": "",
						"updated_at": now,
					}).Error; err != nil {
					m.logger.Warn("Failed to update Inbox status",
						zap.Error(err),
						zap.String("event_id", failure.EventID))
				}
			}

			return nil
		})
	}

	// 失败：更新错误信息
	return internal.DB.Model(&failure).Updates(map[string]interface{}{
		"status":         model.DLQStatusPending,
		"failure_reason": err.Error(),
		"failure_count":  failure.FailureCount + 1,
		"last_failed_at": time.Now(),
	}).Error
}

// RetryBatch 批量重试消息
func (m *DLQManager) RetryBatch(ctx context.Context, ids []uint) (int, int, error) {
	successCount := 0
	failCount := 0

	for _, id := range ids {
		if err := m.RetryMessage(ctx, id); err != nil {
			m.logger.Error("[DLQManager]Failed to retry message",
				zap.Uint("id", id),
				zap.Error(err))
			failCount++
		} else {
			successCount++
		}
	}

	return successCount, failCount, nil
}

// MarkResolved 手动标记为已解决
func (m *DLQManager) MarkResolved(ctx context.Context, id uint, reason string, resolvedBy string) error {
	var failure model.DLQFailure
	if err := internal.DB.WithContext(ctx).First(&failure, id).Error; err != nil {
		return fmt.Errorf("message not found: %w", err)
	}

	if failure.Status == model.DLQStatusResolved {
		return fmt.Errorf("message already resolved")
	}

	m.logger.Info("[DLQManager]Manually marking message as resolved",
		zap.Uint("id", id),
		zap.String("event_id", failure.EventID),
		zap.String("reason", reason))

	now := time.Now()
	return internal.DB.Model(&failure).Updates(map[string]interface{}{
		"status":      model.DLQStatusResolved,
		"resolved_at": &now,
		"resolved_by": resolvedBy,
		"resolution":  reason,
	}).Error
}

// DeleteMessage 删除消息
func (m *DLQManager) DeleteMessage(ctx context.Context, id uint) error {
	m.logger.Info("[DLQManager]Deleting DLQ message", zap.Uint("id", id))
	return internal.DB.WithContext(ctx).Delete(&model.DLQFailure{}, id).Error
}

// DeleteBatch 批量删除消息
func (m *DLQManager) DeleteBatch(ctx context.Context, ids []uint) error {
	m.logger.Info("[DLQManager]Batch deleting DLQ messages", zap.Int("count", len(ids)))
	return internal.DB.WithContext(ctx).Delete(&model.DLQFailure{}, ids).Error
}

// CleanupOldMessages 清理旧消息（已解决且超过保留期的）
func (m *DLQManager) CleanupOldMessages(ctx context.Context, retentionDays int) (int64, error) {
	cutoffTime := time.Now().AddDate(0, 0, -retentionDays)

	m.logger.Info("[DLQManager]Cleaning up old DLQ messages",
		zap.Int("retention_days", retentionDays),
		zap.Time("cutoff_time", cutoffTime))

	result := internal.DB.WithContext(ctx).
		Where("status = ? AND resolved_at < ?", model.DLQStatusResolved, cutoffTime).
		Delete(&model.DLQFailure{})

	if result.Error != nil {
		return 0, result.Error
	}

	m.logger.Info("[DLQManager]Cleaned up old DLQ messages",
		zap.Int64("deleted_count", result.RowsAffected))

	return result.RowsAffected, nil
}

// GetStatistics 获取 DLQ 统计信息
func (m *DLQManager) GetStatistics(ctx context.Context) (map[string]interface{}, error) {
	stats := make(map[string]interface{})

	// 按状态统计
	type StatusCount struct {
		Status model.DLQStatus
		Count  int64
	}
	var statusCounts []StatusCount
	if err := internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Select("status, COUNT(*) as count").
		Group("status").
		Find(&statusCounts).Error; err != nil {
		return nil, err
	}

	statusMap := make(map[string]int64)
	for _, sc := range statusCounts {
		statusMap[string(sc.Status)] = sc.Count
	}
	stats["by_status"] = statusMap

	// 按错误类型统计
	type ErrorCategoryCount struct {
		ErrorCategory string
		Count         int64
	}
	var errorCounts []ErrorCategoryCount
	if err := internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Select("error_category, COUNT(*) as count").
		Group("error_category").
		Find(&errorCounts).Error; err != nil {
		return nil, err
	}

	errorMap := make(map[string]int64)
	for _, ec := range errorCounts {
		errorMap[ec.ErrorCategory] = ec.Count
	}
	stats["by_error_category"] = errorMap

	// 最近 24 小时的失败趋势
	last24h := time.Now().Add(-24 * time.Hour)
	var recentFailures int64
	if err := internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Where("first_failed_at > ?", last24h).
		Count(&recentFailures).Error; err != nil {
		return nil, err
	}
	stats["failures_last_24h"] = recentFailures

	// 最近 24 小时的恢复数
	var recentRecoveries int64
	if err := internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Where("status = ? AND resolved_at > ?", model.DLQStatusResolved, last24h).
		Count(&recentRecoveries).Error; err != nil {
		return nil, err
	}
	stats["recoveries_last_24h"] = recentRecoveries

	// 恢复率
	if recentFailures > 0 {
		stats["recovery_rate"] = float64(recentRecoveries) / float64(recentFailures) * 100
	} else {
		stats["recovery_rate"] = 0.0
	}

	return stats, nil
}
