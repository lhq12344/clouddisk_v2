package kafka

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"runtime/debug"
	"time"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
	"gorm.io/gorm"
)

// DLQProducer 死信队列生产者
type DLQProducer struct {
	producer  sarama.SyncProducer
	dlqTopic  string
	logger    *zap.Logger
}

// NewDLQProducer 创建 DLQ 生产者
func NewDLQProducer(brokers []string, dlqTopic string) (*DLQProducer, error) {
	config := sarama.NewConfig()
	config.Producer.RequiredAcks = sarama.WaitForAll
	config.Producer.Retry.Max = 3
	config.Producer.Return.Successes = true
	config.Producer.Return.Errors = true

	producer, err := sarama.NewSyncProducer(brokers, config)
	if err != nil {
		return nil, fmt.Errorf("failed to create DLQ producer: %w", err)
	}

	return &DLQProducer{
		producer: producer,
		dlqTopic: dlqTopic,
		logger:   internal.Logger,
	}, nil
}

// DLQMessage DLQ 消息结构
type DLQMessage struct {
	OriginalTopic     string    `json:"original_topic"`
	OriginalPartition int32     `json:"original_partition"`
	OriginalOffset    int64     `json:"original_offset"`
	OriginalKey       string    `json:"original_key"`
	OriginalValue     []byte    `json:"original_value"`

	// 失败信息
	FailureReason string    `json:"failure_reason"`
	FailureCount  int       `json:"failure_count"`
	ErrorCategory string    `json:"error_category"`
	ErrorStack    string    `json:"error_stack"`
	FirstFailedAt time.Time `json:"first_failed_at"`
	LastFailedAt  time.Time `json:"last_failed_at"`

	// 业务上下文
	UserID    string `json:"user_id"`
	FileHash  string `json:"file_hash"`
	ObjectKey string `json:"object_key"`
	EventID   string `json:"event_id"`
}

// SendToDLQ 发送消息到死信队列
func (p *DLQProducer) SendToDLQ(ctx context.Context, msg *sarama.ConsumerMessage, err error, retryCount int) error {
	now := time.Now()

	// 解析原始消息获取业务字段
	var payload UploadCmdPayload
	_ = json.Unmarshal(msg.Value, &payload)

	// 构建 DLQ 消息
	dlqMsg := DLQMessage{
		OriginalTopic:     msg.Topic,
		OriginalPartition: msg.Partition,
		OriginalOffset:    msg.Offset,
		OriginalKey:       string(msg.Key),
		OriginalValue:     msg.Value,

		FailureReason: err.Error(),
		FailureCount:  retryCount,
		ErrorCategory: string(CategorizeError(err)),
		ErrorStack:    string(debug.Stack()),
		FirstFailedAt: now,
		LastFailedAt:  now,

		UserID:    payload.TxID,
		FileHash:  payload.Sha1,
		ObjectKey: payload.OssKey,
		EventID:   payload.EventID,
	}

	// 序列化 DLQ 消息
	dlqValue, err := json.Marshal(dlqMsg)
	if err != nil {
		p.logger.Error("Failed to marshal DLQ message", zap.Error(err))
		return err
	}

	// 发送到 Kafka DLQ topic
	kafkaMsg := &sarama.ProducerMessage{
		Topic: p.dlqTopic,
		Key:   sarama.StringEncoder(payload.EventID),
		Value: sarama.ByteEncoder(dlqValue),
		Headers: []sarama.RecordHeader{
			{Key: []byte("x-dlq-source"), Value: []byte(msg.Topic)},
			{Key: []byte("x-dlq-timestamp"), Value: []byte(now.Format(time.RFC3339))},
			{Key: []byte("x-error-category"), Value: []byte(dlqMsg.ErrorCategory)},
		},
	}

	partition, offset, err := p.producer.SendMessage(kafkaMsg)
	if err != nil {
		p.logger.Error("Failed to send message to DLQ",
			zap.Error(err),
			zap.String("event_id", payload.EventID),
			zap.Int64("original_offset", msg.Offset))
		return err
	}

	p.logger.Info("Message sent to DLQ",
		zap.String("event_id", payload.EventID),
		zap.Int32("dlq_partition", partition),
		zap.Int64("dlq_offset", offset),
		zap.Int64("original_offset", msg.Offset),
		zap.String("error_category", dlqMsg.ErrorCategory))

	// 持久化到 MySQL（事务中同时更新 Inbox）
	if err := p.persistToDB(ctx, &dlqMsg); err != nil {
		p.logger.Error("Failed to persist DLQ message to DB",
			zap.Error(err),
			zap.String("event_id", payload.EventID))
		// 不返回错误，因为已经发送到 Kafka DLQ
	}

	return nil
}

// persistToDB 持久化 DLQ 消息到数据库（事务中同时更新 Inbox）
func (p *DLQProducer) persistToDB(ctx context.Context, dlqMsg *DLQMessage) error {
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 1. 创建 DLQ 失败记录
		record := model.DLQFailure{
			Topic:     dlqMsg.OriginalTopic,
			Partition: dlqMsg.OriginalPartition,
			Offset:    dlqMsg.OriginalOffset,

			UserID:    dlqMsg.UserID,
			ObjectKey: dlqMsg.ObjectKey,
			FileHash:  dlqMsg.FileHash,
			EventID:   dlqMsg.EventID,

			OriginalKey:   dlqMsg.OriginalKey,
			OriginalValue: dlqMsg.OriginalValue,

			FailureReason: dlqMsg.FailureReason,
			FailureCount:  dlqMsg.FailureCount,
			ErrorCategory: dlqMsg.ErrorCategory,
			ErrorStack:    dlqMsg.ErrorStack,
			FirstFailedAt: dlqMsg.FirstFailedAt,
			LastFailedAt:  dlqMsg.LastFailedAt,

			Status: model.DLQStatusPending,
		}

		if err := tx.Create(&record).Error; err != nil {
			return fmt.Errorf("failed to create DLQ record: %w", err)
		}

		// 2. 更新 Inbox 状态为 DLQ，释放锁
		if dlqMsg.EventID != "" {
			err := tx.Model(&model.Inbox{}).
				Where("event_id = ?", dlqMsg.EventID).
				Updates(map[string]interface{}{
					"status":         model.InboxDLQ,
					"last_error":     dlqMsg.FailureReason,
					"dlq_failure_id": record.ID,
					"locked_by":      "",        // 释放锁
					"locked_until":   nil,       // 清除锁超时
					"updated_at":     time.Now(),
				}).Error

			if err != nil {
				p.logger.Warn("Failed to update Inbox status to DLQ",
					zap.Error(err),
					zap.String("event_id", dlqMsg.EventID))
				// 不返回错误，因为 DLQ 记录已创建
			} else {
				p.logger.Info("Updated Inbox status to DLQ",
					zap.String("event_id", dlqMsg.EventID),
					zap.Uint("dlq_failure_id", record.ID))
			}
		}

		return nil
	})
}


// Close 关闭生产者
func (p *DLQProducer) Close() error {
	if p.producer != nil {
		return p.producer.Close()
	}
	return nil
}
