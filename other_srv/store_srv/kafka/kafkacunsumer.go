package kafka

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"os"
	"strings"
	"time"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
	"gorm.io/gorm"

	"go_test/other_srv/store_srv/localfile"
	"go_test/other_srv/store_srv/log"
	"sync"
)

type UploadCmdPayload struct {
	TxID      string `json:"tx_id"`
	EventID   string `json:"event_id"`
	FileID    uint   `json:"file_id"`
	Sha1      string `json:"sha1"`
	Size      int32  `json:"size"`
	OssKey    string `json:"oss_key"`
	Content   string `json:"content"`
	Type      string `json:"type"`
	EventType string `json:"event_type"`
}
type Task struct {
	msg     *sarama.ConsumerMessage
	session sarama.ConsumerGroupSession
}

// FileUploadConsumer 文件上传消费者，实现在ConsumerGroupHandler接口
type FileUploadConsumer struct {
	ossClient   *internal.MINIOClient // OSS对象，用于oss操作
	workerCount int                   // 协程池大小
	taskCh      chan *Task
	wg          sync.WaitGroup
	localFile   *localfile.LoadFile
	sessionMu   sync.RWMutex
	session     sarama.ConsumerGroupSession
	instanceID  string
}

// NewFileUploadConsumer 构造函数，创建消费者处理实例并初始化协程池
func NewFileUploadConsumer(ctx context.Context, workerCount int) *FileUploadConsumer {
	host, _ := os.Hostname()
	instanceID := fmt.Sprintf("%s-%d", host, os.Getpid())

	c := &FileUploadConsumer{
		ossClient:   internal.MinIOClient,
		workerCount: workerCount,
		taskCh:      make(chan *Task, workerCount*2), // 缓冲队列
		localFile:   &localfile.LoadFile{},
		instanceID:  instanceID,
	}
	// 启动 workerCount 个后台协程从队列中消费任务
	for i := 0; i < workerCount; i++ {
		c.wg.Add(1)
		go func(ctx context.Context) {
			defer c.wg.Done()
			for t := range c.taskCh {
				err := c.processMessage(ctx, t.msg)
				if err == nil {
					// 仅在成功时提交 offset
					// session 可能已结束：这里做一次保护
					if t.session != nil && t.session.Context().Err() == nil {
						t.session.MarkMessage(t.msg, "")
					}
				} else {
					// 失败不 Mark，让 Kafka 重投（至少一次）
					log.Logger.Error("[NewFileUploadConsumer]process message failed", zap.Error(err),
						zap.Int32("partition", t.msg.Partition),
						zap.Int64("offset", t.msg.Offset))
				}
			}
		}(ctx)
	}
	return c
}

// Setup 在新的会话开始时调用（如平衡分区后）
func (c *FileUploadConsumer) Setup(session sarama.ConsumerGroupSession) error {
	log.Logger.Info("[Setup]Consumer session setup")
	return nil
}

// Cleanup 在会话结束时调用（如发生再均衡前）
func (c *FileUploadConsumer) Cleanup(session sarama.ConsumerGroupSession) error {
	log.Logger.Info("[Cleanup]Consumer session cleanup, all pending tasks done.")
	return nil
}

// ConsumeClaim 消费组对指定Topic/分区的消费逻辑
func (c *FileUploadConsumer) ConsumeClaim(session sarama.ConsumerGroupSession, claim sarama.ConsumerGroupClaim) error {
	for msg := range claim.Messages() {
		// rebalance 时避免阻塞投递
		select {
		case c.taskCh <- &Task{msg: msg, session: session}:
		case <-session.Context().Done():
			return nil
		}
	}
	return nil
}

// 实际处理单条消息的逻辑，包括反序列化、缓存检查和OSS上传
func (c *FileUploadConsumer) processMessage(ctx context.Context, msg *sarama.ConsumerMessage) error {
	var p UploadCmdPayload
	if err := json.Unmarshal(msg.Value, &p); err != nil {
		return fmt.Errorf("unmarshal: %w", err)
	}
	if p.EventID == "" || p.Sha1 == "" || p.OssKey == "" {
		return fmt.Errorf("invalid payload: event_id/sha1/oss_key required")
	}

	// 1) Inbox 幂等领取
	acquired, done, err := c.tryBeginInbox(ctx, p.EventID)
	if err != nil {
		return err
	}
	if done {
		return nil // 已处理过，允许 MarkMessage
	}
	if !acquired {
		return fmt.Errorf("inbox locked by other worker, retry later")
	}

	// 3) OSS 是否已存在（用 OssKey）
	ossExists, err := c.ossClient.MinIOObjectExists(p.OssKey)
	if err != nil {
		// 检查失败不应直接跳过；继续走流程，但记录一下
		log.Logger.Warn("OSS existence check failed", zap.Error(err), zap.String("ossKey", p.OssKey))
		ossExists = false
	}

	// 6) 上传 OSS（若不存在）
	if !ossExists {
		switch p.EventType {
		case model.LoadFile:
			if err := c.ossClient.MinIOUploadBytes([]byte(p.Content), p.OssKey, p.Type); err != nil {
				_ = c.markInboxFailed(ctx, p.EventID, err.Error())
				return fmt.Errorf("upload oss: %w", err)
			}
		}
	}

	// 7) 更新业务状态（files.status=READY 等）+ inbox DONE
	if err := c.finalizeSuccess(ctx, &p); err != nil {
		_ = c.markInboxFailed(ctx, p.EventID, err.Error())
		return err
	}
	return nil
}

func (c *FileUploadConsumer) finalizeSuccess(ctx context.Context, p *UploadCmdPayload) error {
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 例：更新 files 状态（按你的真实表结构改）
		if err := tx.Model(&model.File{}).Where("sha1 = ?", p.Sha1).
			Update("status", model.FileSuccess).Error; err != nil {
			return err
		}
		// inbox done
		if err := tx.Model(&model.Inbox{}).
			Where("event_id = ? AND locked_by = ?", p.EventID, c.instanceID).
			Updates(map[string]any{
				"status":       model.InboxDone,
				"locked_by":    "",
				"locked_until": nil,
				"updated_at":   time.Now(),
			}).Error; err != nil {
			return err
		}
		return nil
	})
}

func (c *FileUploadConsumer) tryBeginInbox(ctx context.Context, eventID string) (acquired bool, done bool, err error) {
	now := time.Now()
	lockUntil := now.Add(2 * time.Minute)

	rec := model.Inbox{
		EventID:     eventID,
		Status:      model.InboxProcessing,
		LockedBy:    c.instanceID,
		LockedUntil: &lockUntil,
		Attempts:    1,
	}

	// 1) 先插入
	if err := internal.DB.WithContext(ctx).Create(&rec).Error; err == nil {
		return true, false, nil
	} else {
		// 2) 不是重复键就返回错误
		if !isDupKey(err) {
			return false, false, err
		}
	}

	// 3) 查现状
	var existing model.Inbox
	if err := internal.DB.WithContext(ctx).First(&existing, "event_id = ?", eventID).Error; err != nil {
		return false, false, err
	}
	if existing.Status == model.InboxDone {
		return false, true, nil
	}

	// 4) 尝试接管（锁过期才接管）
	res := internal.DB.WithContext(ctx).Model(&model.Inbox{}).
		Where("event_id = ? AND status <> ? AND (locked_until IS NULL OR locked_until < ?)",
			eventID, model.InboxDone, now).
		Updates(map[string]any{
			"status":       model.InboxProcessing,
			"locked_by":    c.instanceID,
			"locked_until": &lockUntil,
			"attempts":     gorm.Expr("attempts + 1"),
			"updated_at":   now,
		})

	if res.Error != nil {
		return false, false, res.Error
	}
	if res.RowsAffected == 0 {
		// 别的实例仍持锁处理中
		return false, false, nil
	}
	return true, false, nil
}

func (c *FileUploadConsumer) markInboxFailed(ctx context.Context, eventID, errMsg string) error {
	now := time.Now()
	return internal.DB.WithContext(ctx).Model(&model.Inbox{}).
		Where("event_id = ? AND locked_by = ?", eventID, c.instanceID).
		Updates(map[string]any{
			"status":       model.InboxFailed,
			"last_error":   errMsg,
			"locked_until": nil,
			"locked_by":    "",
			"updated_at":   now,
		}).Error
}

// 你可以用 mysql driver 的错误码 1062；这里给一个通用兜底
func isDupKey(err error) bool {
	if err == nil {
		return false
	}
	s := err.Error()
	return strings.Contains(s, "Duplicate entry") || strings.Contains(s, "Error 1062")
}

// getSession 安全地读取当前的ConsumerGroupSession
func (c *FileUploadConsumer) getSession() sarama.ConsumerGroupSession {
	c.sessionMu.RLock()
	defer c.sessionMu.RUnlock()
	return c.session
}
