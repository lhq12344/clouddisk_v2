package main

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"os"
	"sync"
	"time"

	"go_test/backword_part/log"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

type OutboxDispatcher struct {
	db           *gorm.DB
	producer     sarama.SyncProducer
	instanceID   string
	pollInterval time.Duration //每隔多久去 DB 看一眼有没有待发送的 outbox。
	batchSize    int           //每次从 outbox 里“抢占（claim）”并取出来的最大记录数
	workerCount  int           //并发发送 Kafka 的 worker 数量（goroutine 数）
	lockTTL      time.Duration //抢占锁的“租约时间”。dispatcher 抢占 outbox 记录后，会写入 locked_until = now + lockTTL。在 locked_until 到期前，其他实例不会再抢这条记录。
}

func NewOutboxDispatcher(db *gorm.DB, producer sarama.SyncProducer) *OutboxDispatcher {
	host, _ := os.Hostname()
	instanceID := fmt.Sprintf("%s-%d", host, os.Getpid())

	return &OutboxDispatcher{
		db:           db,
		producer:     producer,
		instanceID:   instanceID,
		pollInterval: 2 * time.Second,
		batchSize:    50,
		workerCount:  8,
		lockTTL:      30 * time.Second,
	}
}

// Start 启动 dispatcher：轮询 outbox，抢占一批记录，分发给 worker 发送 Kafka
func (d *OutboxDispatcher) Start(ctx context.Context) {
	jobs := make(chan model.Outbox, d.batchSize*2)
	var wg sync.WaitGroup

	// workers
	for i := 0; i < d.workerCount; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			d.workerLoop(ctx, jobs)
		}()
	}

	// poller
	ticker := time.NewTicker(d.pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			close(jobs)
			wg.Wait()
			log.Logger.Info("[Start]Outbox dispatcher stopped")
			return
		case <-ticker.C:
			items, err := d.claimBatch(ctx)
			if err != nil {
				log.Logger.Error("[Start]Outbox claim batch failed", zap.Error(err))
				continue
			}
			for _, ob := range items {
				select {
				case jobs <- ob:
				case <-ctx.Done():
					close(jobs)
					wg.Wait()
					return
				}
			}
		}
	}
}

func (d *OutboxDispatcher) workerLoop(ctx context.Context, jobs <-chan model.Outbox) {
	for {
		select {
		case <-ctx.Done():
			return
		case ob, ok := <-jobs:
			if !ok {
				return
			}
			if err := d.sendOne(ctx, &ob); err != nil {
				// 失败：标 FAILED + 退避
				if uerr := d.markFailed(ctx, ob.ID, ob.RetryCount, err); uerr != nil {
					log.Logger.Error("[workerLoop]markFailed failed", zap.Uint("id", ob.ID), zap.Error(uerr))
				}
				continue
			}
			// 成功：标 SENT
			if err := d.markSent(ctx, ob.ID); err != nil {
				// 这里即便更新失败也没法回滚 Kafka 已发送；只能依赖消费端幂等（inbox）
				log.Logger.Error("[workerLoop]markSent failed (message already sent)", zap.Uint("id", ob.ID), zap.Error(err))
			}
		}
	}
}

// claimBatch 抢占一批可发送的 outbox：将 NEW/FAILED 且 next_retry_at<=now 的记录置为 SENDING 并上锁
func (d *OutboxDispatcher) claimBatch(ctx context.Context) ([]model.Outbox, error) {
	now := time.Now()
	lockUntil := now.Add(d.lockTTL)

	var items []model.Outbox

	err := d.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 1) 锁定一批可发送记录：FOR UPDATE SKIP LOCKED
		//    SKIP LOCKED：别的实例已经锁住的行会被跳过，不会阻塞等待
		err := tx.Clauses(clause.Locking{Strength: "UPDATE", Options: "SKIP LOCKED"}).
			Where("status IN ? AND next_retry_at <= ? AND (locked_until IS NULL OR locked_until < ?)",
				[]model.OutboxStatus{model.OutboxNew, model.OutboxFailed}, now, now).
			Order("id").
			Limit(d.batchSize).
			Find(&items).Error
		if err != nil {
			log.Logger.Error("[claimBatch] error", zap.Error(err))
			return err
		}
		if len(items) == 0 {
			return nil
		}

		// 2) 更新为 SENDING + 写锁信息（仍在同一事务里，语义最强）
		ids := make([]uint, 0, len(items))
		for i := range items {
			ids = append(ids, items[i].ID)
		}

		if err := tx.Model(&model.Outbox{}).
			Where("id IN ?", ids).
			Updates(map[string]any{
				"status":       model.OutboxSending,
				"locked_by":    d.instanceID,
				"locked_until": &lockUntil,
				"updated_at":   now,
			}).Error; err != nil {
			return err
		}

		// 3) 可选：把内存中的 items 字段同步一下，避免再查库
		for i := range items {
			items[i].Status = model.OutboxSending
			items[i].LockedBy = d.instanceID
			items[i].LockedUntil = &lockUntil
		}

		return nil
	})

	return items, err
}

func (d *OutboxDispatcher) sendOne(ctx context.Context, ob *model.Outbox) error {
	if d.producer == nil {
		return fmt.Errorf("[sendOne]kafka producer not initialized")
	}

	msg := &sarama.ProducerMessage{
		Topic: ob.Topic,
		Value: sarama.ByteEncoder([]byte(ob.Payload)),
	}
	if ob.Key != "" {
		msg.Key = sarama.StringEncoder(ob.Key)
	}

	// 从 Outbox.Headers 解析并传播到 Kafka headers
	if ob.Headers != "" && ob.Headers != "{}" {
		var headers map[string]string
		if err := json.Unmarshal([]byte(ob.Headers), &headers); err == nil {
			for k, v := range headers {
				msg.Headers = append(msg.Headers, sarama.RecordHeader{
					Key:   []byte(k),
					Value: []byte(v),
				})
			}
		}
	}

	_, _, err := d.producer.SendMessage(msg)
	return err
}

func (d *OutboxDispatcher) markSent(ctx context.Context, id uint) error {
	now := time.Now()
	return d.db.WithContext(ctx).Model(&model.Outbox{}).
		Where("id = ? AND status = ? AND locked_by = ?", id, model.OutboxSending, d.instanceID).
		Updates(map[string]any{
			"status":       model.OutboxSent,
			"locked_by":    "",
			"locked_until": nil,
			"last_error":   "",
			"updated_at":   now,
		}).Error
}

func (d *OutboxDispatcher) markFailed(ctx context.Context, id uint, retryCount int, sendErr error) error {
	now := time.Now()
	next := now.Add(backoff(retryCount + 1))

	return d.db.WithContext(ctx).Model(&model.Outbox{}).
		Where("id = ? AND status = ? AND locked_by = ?", id, model.OutboxSending, d.instanceID).
		Updates(map[string]any{
			"status":        model.OutboxFailed,
			"retry_count":   retryCount + 1,
			"next_retry_at": next,
			"last_error":    sendErr.Error(),
			"locked_by":     "",
			"locked_until":  nil,
			"updated_at":    now,
		}).Error
}

// backoff 简单指数退避（上限 5 分钟）
func backoff(n int) time.Duration {
	if n <= 0 {
		return time.Second
	}
	d := time.Second * time.Duration(1<<min(n, 10)) // 2^10=1024s
	if d > 5*time.Minute {
		d = 5 * time.Minute
	}
	return d
}
