package kafka

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"time"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
	"gorm.io/gorm"
)

// DLQConsumerConfig DLQ 消费者配置
type DLQConsumerConfig struct {
	PollInterval time.Duration // 轮询间隔
	BatchSize    int           // 每次处理的消息数
	MaxRetries   int           // DLQ 消息的最大重试次数
}

// DefaultDLQConsumerConfig 默认 DLQ 消费者配置
func DefaultDLQConsumerConfig() DLQConsumerConfig {
	return DLQConsumerConfig{
		PollInterval: 5 * time.Minute,
		BatchSize:    10,
		MaxRetries:   1,
	}
}

// DLQConsumer DLQ 消费者
type DLQConsumer struct {
	consumer      sarama.ConsumerGroup
	config        DLQConsumerConfig
	processor     *FileUploadConsumer // 复用主消费者的处理逻辑
	logger        *zap.Logger
	ctx           context.Context
	cancel        context.CancelFunc
}

// NewDLQConsumer 创建 DLQ 消费者
func NewDLQConsumer(
	brokers []string,
	groupID string,
	dlqTopic string,
	processor *FileUploadConsumer,
	config DLQConsumerConfig,
) (*DLQConsumer, error) {
	cfg := sarama.NewConfig()
	cfg.Version = sarama.V3_5_0_0
	cfg.Consumer.Group.Rebalance.Strategy = sarama.BalanceStrategyRange
	cfg.Consumer.Offsets.Initial = sarama.OffsetOldest // DLQ 从最早的消息开始
	cfg.Consumer.Offsets.AutoCommit.Enable = true
	cfg.Consumer.Offsets.AutoCommit.Interval = 10 * time.Second
	cfg.Consumer.Return.Errors = true

	consumerGroup, err := sarama.NewConsumerGroup(brokers, groupID, cfg)
	if err != nil {
		return nil, fmt.Errorf("failed to create DLQ consumer group: %w", err)
	}

	ctx, cancel := context.WithCancel(context.Background())

	return &DLQConsumer{
		consumer:  consumerGroup,
		config:    config,
		processor: processor,
		logger:    internal.Logger,
		ctx:       ctx,
		cancel:    cancel,
	}, nil
}

// Start 启动 DLQ 消费者（低频轮询模式）
func (d *DLQConsumer) Start() {
	d.logger.Info("[DLQConsumer]Starting DLQ consumer",
		zap.Duration("poll_interval", d.config.PollInterval),
		zap.Int("batch_size", d.config.BatchSize))

	ticker := time.NewTicker(d.config.PollInterval)
	defer ticker.Stop()

	// 后台监听消费错误
	go func() {
		for err := range d.consumer.Errors() {
			d.logger.Error("[DLQConsumer]Consumer group error", zap.Error(err))
		}
	}()

	for {
		select {
		case <-d.ctx.Done():
			d.logger.Info("[DLQConsumer]DLQ consumer stopped")
			return
		case <-ticker.C:
			d.processBatch()
		}
	}
}

// processBatch 处理一批 DLQ 消息
func (d *DLQConsumer) processBatch() {
	d.logger.Info("[DLQConsumer]Processing DLQ batch")

	// 从数据库查询待处理的 DLQ 消息
	var failures []model.DLQFailure
	err := internal.DB.Where("status = ?", model.DLQStatusPending).
		Order("first_failed_at ASC").
		Limit(d.config.BatchSize).
		Find(&failures).Error

	if err != nil {
		d.logger.Error("[DLQConsumer]Failed to query DLQ failures", zap.Error(err))
		return
	}

	if len(failures) == 0 {
		d.logger.Debug("[DLQConsumer]No pending DLQ messages")
		return
	}

	d.logger.Info("[DLQConsumer]Found pending DLQ messages", zap.Int("count", len(failures)))

	for _, failure := range failures {
		d.processFailure(&failure)
	}
}

// processFailure 处理单个 DLQ 失败记录
func (d *DLQConsumer) processFailure(failure *model.DLQFailure) {
	d.logger.Info("[DLQConsumer]Processing DLQ failure",
		zap.Uint("id", failure.ID),
		zap.String("event_id", failure.EventID),
		zap.Int("failure_count", failure.FailureCount))

	// 更新状态为 retrying
	if err := d.updateStatus(failure.ID, model.DLQStatusRetrying); err != nil {
		d.logger.Error("[DLQConsumer]Failed to update status to retrying", zap.Error(err))
		return
	}

	// 重新构造 Kafka 消息
	msg := &sarama.ConsumerMessage{
		Topic:     failure.Topic,
		Partition: failure.Partition,
		Offset:    failure.Offset,
		Key:       []byte(failure.OriginalKey),
		Value:     failure.OriginalValue,
	}

	// 尝试重新处理
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()

	err := d.processor.processMessage(ctx, msg)

	if err == nil {
		// 成功：标记为 resolved
		d.logger.Info("[DLQConsumer]DLQ message processed successfully",
			zap.Uint("id", failure.ID),
			zap.String("event_id", failure.EventID))

		if err := d.markResolved(failure.ID, "Auto-recovered by DLQ consumer"); err != nil {
			d.logger.Error("[DLQConsumer]Failed to mark as resolved", zap.Error(err))
		}

		// 发送恢复通知（可选）
		d.notifyRecovery(failure)
	} else {
		// 仍然失败
		d.logger.Error("[DLQConsumer]DLQ message processing failed",
			zap.Uint("id", failure.ID),
			zap.String("event_id", failure.EventID),
			zap.Error(err))

		// 更新失败次数
		newFailureCount := failure.FailureCount + 1

		if newFailureCount > d.config.MaxRetries {
			// 超过 DLQ 重试次数：标记为最终失败
			if err := d.markFailed(failure.ID, err.Error()); err != nil {
				d.logger.Error("[DLQConsumer]Failed to mark as failed", zap.Error(err))
			}

			// 发送告警
			d.notifyCriticalFailure(failure, err)
		} else {
			// 重置为 pending，等待下次重试
			if err := d.updateStatusWithError(failure.ID, model.DLQStatusPending, err.Error(), newFailureCount); err != nil {
				d.logger.Error("[DLQConsumer]Failed to update status", zap.Error(err))
			}
		}
	}
}

// updateStatus 更新 DLQ 状态
func (d *DLQConsumer) updateStatus(id uint, status model.DLQStatus) error {
	return internal.DB.Model(&model.DLQFailure{}).
		Where("id = ?", id).
		Update("status", status).Error
}

// updateStatusWithError 更新状态并记录错误
func (d *DLQConsumer) updateStatusWithError(id uint, status model.DLQStatus, errMsg string, failureCount int) error {
	return internal.DB.Model(&model.DLQFailure{}).
		Where("id = ?", id).
		Updates(map[string]interface{}{
			"status":         status,
			"failure_reason": errMsg,
			"failure_count":  failureCount,
			"last_failed_at": time.Now(),
		}).Error
}

// markResolved 标记为已解决（同时更新 Inbox）
func (d *DLQConsumer) markResolved(id uint, resolution string) error {
	return internal.DB.Transaction(func(tx *gorm.DB) error {
		// 1. 查询 DLQ 记录
		var failure model.DLQFailure
		if err := tx.First(&failure, id).Error; err != nil {
			return err
		}

		// 2. 更新 DLQ 状态为 resolved
		now := time.Now()
		if err := tx.Model(&failure).Updates(map[string]interface{}{
			"status":      model.DLQStatusResolved,
			"resolved_at": &now,
			"resolved_by": "dlq_consumer",
			"resolution":  resolution,
		}).Error; err != nil {
			return err
		}

		// 3. 同步更新 Inbox 状态为 DONE
		if failure.EventID != "" {
			err := tx.Model(&model.Inbox{}).
				Where("event_id = ?", failure.EventID).
				Updates(map[string]interface{}{
					"status":     model.InboxDone,
					"last_error": "",
					"updated_at": now,
				}).Error

			if err != nil {
				d.logger.Warn("Failed to update Inbox status to DONE",
					zap.Error(err),
					zap.String("event_id", failure.EventID))
				// 不返回错误，DLQ 已标记为 resolved
			} else {
				d.logger.Info("Updated Inbox status to DONE",
					zap.String("event_id", failure.EventID))
			}
		}

		return nil
	})
}

// markFailed 标记为最终失败
func (d *DLQConsumer) markFailed(id uint, errMsg string) error {
	return internal.DB.Model(&model.DLQFailure{}).
		Where("id = ?", id).
		Updates(map[string]interface{}{
			"status":         model.DLQStatusFailed,
			"failure_reason": errMsg,
			"last_failed_at": time.Now(),
		}).Error
}

// notifyRecovery 发送恢复通知
func (d *DLQConsumer) notifyRecovery(failure *model.DLQFailure) {
	d.logger.Info("[DLQConsumer]DLQ message recovered",
		zap.Uint("id", failure.ID),
		zap.String("event_id", failure.EventID),
		zap.String("user_id", failure.UserID))

	// TODO: 集成告警系统（Slack/Email/钉钉等）
}

// notifyCriticalFailure 发送严重失败告警
func (d *DLQConsumer) notifyCriticalFailure(failure *model.DLQFailure, err error) {
	d.logger.Error("[DLQConsumer]CRITICAL: DLQ message failed permanently",
		zap.Uint("id", failure.ID),
		zap.String("event_id", failure.EventID),
		zap.String("user_id", failure.UserID),
		zap.Int("failure_count", failure.FailureCount),
		zap.Error(err))

	// TODO: 集成告警系统（Slack/Email/钉钉等）
	// 示例：发送到告警通道
	// alerter.SendAlert(AlertLevelCritical, fmt.Sprintf("DLQ message %d failed permanently", failure.ID))
}

// Stop 停止 DLQ 消费者
func (d *DLQConsumer) Stop() error {
	d.logger.Info("[DLQConsumer]Stopping DLQ consumer")
	d.cancel()
	return d.consumer.Close()
}

// GetMetrics 获取 DLQ 指标
func (d *DLQConsumer) GetMetrics(ctx context.Context) (*model.DLQMetrics, error) {
	var metrics model.DLQMetrics

	// 统计各状态的消息数
	type StatusCount struct {
		Status model.DLQStatus
		Count  int64
	}

	var statusCounts []StatusCount
	err := internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Select("status, COUNT(*) as count").
		Group("status").
		Find(&statusCounts).Error

	if err != nil {
		return nil, err
	}

	for _, sc := range statusCounts {
		switch sc.Status {
		case model.DLQStatusPending:
			metrics.DLQPendingCount = sc.Count
		case model.DLQStatusRetrying:
			metrics.DLQRetryingCount = sc.Count
		case model.DLQStatusFailed:
			metrics.DLQFailedCount = sc.Count
		case model.DLQStatusResolved:
			metrics.DLQResolvedCount = sc.Count
		}
	}

	metrics.DLQMessageCount = metrics.DLQPendingCount + metrics.DLQRetryingCount +
		metrics.DLQFailedCount + metrics.DLQResolvedCount

	// 统计错误类型
	type ErrorCategoryCount struct {
		ErrorCategory string
		Count         int64
	}

	var errorCounts []ErrorCategoryCount
	err = internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Select("error_category, COUNT(*) as count").
		Group("error_category").
		Find(&errorCounts).Error

	if err != nil {
		return nil, err
	}

	for _, ec := range errorCounts {
		switch ec.ErrorCategory {
		case "retryable":
			metrics.RetryableErrors = ec.Count
		case "non_retryable":
			metrics.NonRetryableErrors = ec.Count
		case "unknown":
			metrics.UnknownErrors = ec.Count
		}
	}

	// 计算增长率（最近 1 分钟）
	oneMinuteAgo := time.Now().Add(-1 * time.Minute)
	var recentCount int64
	err = internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Where("created_at > ?", oneMinuteAgo).
		Count(&recentCount).Error

	if err == nil {
		metrics.DLQGrowthRate = float64(recentCount)
	}

	// 计算恢复率（最近 1 分钟）
	var recentRecovered int64
	err = internal.DB.WithContext(ctx).
		Model(&model.DLQFailure{}).
		Where("status = ? AND resolved_at > ?", model.DLQStatusResolved, oneMinuteAgo).
		Count(&recentRecovered).Error

	if err == nil {
		metrics.DLQRecoveryRate = float64(recentRecovered)
	}

	metrics.LastUpdated = time.Now()

	return &metrics, nil
}
