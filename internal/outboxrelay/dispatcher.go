package outboxrelay

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

type Dispatcher struct {
	db           *gorm.DB
	producer     sarama.SyncProducer
	instanceID   string
	pollInterval time.Duration
	batchSize    int
	workerCount  int
	lockTTL      time.Duration
}

func NewDispatcher(db *gorm.DB, producer sarama.SyncProducer) *Dispatcher {
	host, _ := os.Hostname()
	return &Dispatcher{
		db:           db,
		producer:     producer,
		instanceID:   fmt.Sprintf("%s-%d", host, os.Getpid()),
		pollInterval: 2 * time.Second,
		batchSize:    50,
		workerCount:  8,
		lockTTL:      30 * time.Second,
	}
}

func (d *Dispatcher) Start(ctx context.Context) {
	if d.db == nil {
		internal.Logger.Error("[outbox-relay] dispatcher cannot start: db is not initialized")
		return
	}
	if d.producer == nil {
		internal.Logger.Error("[outbox-relay] dispatcher cannot start: kafka producer is not initialized")
		return
	}

	jobs := make(chan model.Outbox, d.batchSize*2)
	var wg sync.WaitGroup

	for i := 0; i < d.workerCount; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			d.workerLoop(ctx, jobs)
		}()
	}

	ticker := time.NewTicker(d.pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			close(jobs)
			wg.Wait()
			internal.Logger.Info("[outbox-relay] dispatcher stopped")
			return
		case <-ticker.C:
			items, err := d.claimBatch(ctx)
			if err != nil {
				internal.Logger.Error("[outbox-relay] claim batch failed", zap.Error(err))
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

func (d *Dispatcher) workerLoop(ctx context.Context, jobs <-chan model.Outbox) {
	for {
		select {
		case <-ctx.Done():
			return
		case ob, ok := <-jobs:
			if !ok {
				return
			}
			if err := d.sendOne(ctx, &ob); err != nil {
				if uerr := d.markFailed(ctx, ob.ID, ob.RetryCount, err); uerr != nil {
					internal.Logger.Error("[outbox-relay] mark failed failed", zap.Uint("id", ob.ID), zap.Error(uerr))
				}
				continue
			}
			if err := d.markSent(ctx, ob.ID); err != nil {
				internal.Logger.Error("[outbox-relay] mark sent failed after Kafka send", zap.Uint("id", ob.ID), zap.Error(err))
			}
		}
	}
}

func (d *Dispatcher) claimBatch(ctx context.Context) ([]model.Outbox, error) {
	now := time.Now()
	lockUntil := now.Add(d.lockTTL)
	var items []model.Outbox

	err := d.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if err := tx.Clauses(clause.Locking{Strength: "UPDATE", Options: "SKIP LOCKED"}).
			Where("status IN ? AND next_retry_at <= ? AND (locked_until IS NULL OR locked_until < ?)",
				[]model.OutboxStatus{model.OutboxNew, model.OutboxFailed}, now, now).
			Order("id").
			Limit(d.batchSize).
			Find(&items).Error; err != nil {
			return err
		}
		if len(items) == 0 {
			return nil
		}

		ids := make([]uint, 0, len(items))
		for i := range items {
			ids = append(ids, items[i].ID)
		}
		result := tx.Model(&model.Outbox{}).
			Where("id IN ?", ids).
			Updates(map[string]any{
				"status":       model.OutboxSending,
				"locked_by":    d.instanceID,
				"locked_until": &lockUntil,
				"updated_at":   now,
			})
		if result.Error != nil {
			return result.Error
		}
		if result.RowsAffected != int64(len(ids)) {
			return fmt.Errorf("claimed %d outbox rows, updated %d", len(ids), result.RowsAffected)
		}

		for i := range items {
			items[i].Status = model.OutboxSending
			items[i].LockedBy = d.instanceID
			items[i].LockedUntil = &lockUntil
		}
		return nil
	})

	return items, err
}

func parseHeaders(raw string) ([]sarama.RecordHeader, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" || raw == "{}" {
		return nil, nil
	}

	var headers map[string]string
	if err := json.Unmarshal([]byte(raw), &headers); err != nil {
		return nil, fmt.Errorf("invalid outbox headers JSON: %w", err)
	}
	parsed := make([]sarama.RecordHeader, 0, len(headers))
	for k, v := range headers {
		if strings.TrimSpace(k) == "" {
			return nil, fmt.Errorf("invalid outbox headers: header key is empty")
		}
		parsed = append(parsed, sarama.RecordHeader{Key: []byte(k), Value: []byte(v)})
	}
	return parsed, nil
}

func (d *Dispatcher) sendOne(ctx context.Context, ob *model.Outbox) error {
	if d.producer == nil {
		return fmt.Errorf("kafka producer not initialized")
	}

	msg := &sarama.ProducerMessage{Topic: ob.Topic, Value: sarama.ByteEncoder([]byte(ob.Payload))}
	if ob.Key != "" {
		msg.Key = sarama.StringEncoder(ob.Key)
	}
	headers, err := parseHeaders(ob.Headers)
	if err != nil {
		return err
	}
	msg.Headers = headers

	_, _, err = d.producer.SendMessage(msg)
	return err
}

func (d *Dispatcher) markSent(ctx context.Context, id uint) error {
	now := time.Now()
	if d.db == nil {
		return fmt.Errorf("db is not initialized")
	}
	result := d.db.WithContext(ctx).Model(&model.Outbox{}).
		Where("id = ? AND status = ? AND locked_by = ?", id, model.OutboxSending, d.instanceID).
		Updates(map[string]any{"status": model.OutboxSent, "locked_by": "", "locked_until": nil, "last_error": "", "updated_at": now})
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected != 1 {
		return fmt.Errorf("outbox row %d was not marked sent by lease owner %q", id, d.instanceID)
	}
	return nil
}

func (d *Dispatcher) markFailed(ctx context.Context, id uint, retryCount int, sendErr error) error {
	now := time.Now()
	if d.db == nil {
		return fmt.Errorf("db is not initialized")
	}
	result := d.db.WithContext(ctx).Model(&model.Outbox{}).
		Where("id = ? AND status = ? AND locked_by = ?", id, model.OutboxSending, d.instanceID).
		Updates(map[string]any{
			"status":        model.OutboxFailed,
			"retry_count":   retryCount + 1,
			"next_retry_at": now.Add(backoff(retryCount + 1)),
			"last_error":    sendErr.Error(),
			"locked_by":     "",
			"locked_until":  nil,
			"updated_at":    now,
		})
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected != 1 {
		return fmt.Errorf("outbox row %d was not marked failed by lease owner %q", id, d.instanceID)
	}
	return nil
}

func backoff(n int) time.Duration {
	if n <= 0 {
		return time.Second
	}
	d := time.Second * time.Duration(1<<min(n, 10))
	if d > 5*time.Minute {
		d = 5 * time.Minute
	}
	return d
}
