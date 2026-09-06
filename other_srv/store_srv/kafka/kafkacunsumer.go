package kafka

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"os"
	"strings"
	"time"

	"github.com/IBM/sarama"
	"github.com/minio/minio-go/v7"
	"go.uber.org/zap"
	"gorm.io/gorm"
)

// ErrInboxLocked 表示 Inbox 正被其他实例处理，当前分区不应越过这条消息推进 offset。
var ErrInboxLocked = errors.New("inbox locked by other worker")

var errFileScanAlreadyConverged = errors.New("file scan already converged")

type consumeAction int

const (
	consumeRetryLater consumeAction = iota
	consumeCommit
)

// FileUploadConsumer 复用既有 outbox/inbox/Kafka 框架，但消息语义已变为“上传完成待病毒扫描”。
type FileUploadConsumer struct {
	ossClient   *internal.MINIOClient
	instanceID  string
	retryConfig RetryConfig
	dlqProducer *DLQProducer
}

func NewFileUploadConsumer(_ context.Context, _ int, dlqProducer *DLQProducer) *FileUploadConsumer {
	host, _ := osHostname()
	instanceID := fmt.Sprintf("%s-%d", host, osPID())

	return &FileUploadConsumer{
		ossClient:   internal.MinIOClient,
		instanceID:  instanceID,
		retryConfig: DefaultRetryConfig(),
		dlqProducer: dlqProducer,
	}
}

func (c *FileUploadConsumer) Setup(session sarama.ConsumerGroupSession) error {
	internal.Logger.Info("[FileUploadConsumer]consumer session setup")
	return nil
}

func (c *FileUploadConsumer) Cleanup(session sarama.ConsumerGroupSession) error {
	internal.Logger.Info("[FileUploadConsumer]consumer session cleanup")
	return nil
}

func (c *FileUploadConsumer) ConsumeClaim(session sarama.ConsumerGroupSession, claim sarama.ConsumerGroupClaim) error {
	for {
		select {
		case <-session.Context().Done():
			return nil
		case msg, ok := <-claim.Messages():
			if !ok {
				return nil
			}
			action, err := c.processMessageWithRetry(session.Context(), msg)
			switch action {
			case consumeCommit:
				if err != nil {
					internal.Logger.Error("[FileUploadConsumer]message converged with terminal failure, committing offset",
						zap.Error(err),
						zap.Int32("partition", msg.Partition),
						zap.Int64("offset", msg.Offset))
				}
				c.commitMessage(session, msg)
			case consumeRetryLater:
				if err != nil && !errors.Is(err, context.Canceled) {
					internal.Logger.Warn("[FileUploadConsumer]message not converged, leaving offset uncommitted",
						zap.Error(err),
						zap.Int32("partition", msg.Partition),
						zap.Int64("offset", msg.Offset))
				}
			}
		}
	}
}

func (c *FileUploadConsumer) commitMessage(session sarama.ConsumerGroupSession, msg *sarama.ConsumerMessage) {
	if session == nil || msg == nil {
		return
	}
	if session.Context().Err() != nil {
		return
	}
	session.MarkMessage(msg, "")
	session.Commit()
}

func (c *FileUploadConsumer) processMessageWithRetry(ctx context.Context, msg *sarama.ConsumerMessage) (consumeAction, error) {
	var (
		lastErr        error
		retryCount     int
		lockRetryCount int
	)

	for retryCount <= c.retryConfig.MaxRetries {
		if ctx.Err() != nil {
			return consumeRetryLater, ctx.Err()
		}

		err := c.processMessage(ctx, msg)
		if err == nil {
			return consumeCommit, nil
		}

		lastErr = err
		if errors.Is(err, ErrInboxLocked) {
			lockRetryCount++
			backoff := c.calculateInboxLockBackoff(lockRetryCount)
			internal.Logger.Debug("[FileUploadConsumer]inbox locked, waiting before retry",
				zap.Int64("offset", msg.Offset),
				zap.Int("attempt", lockRetryCount),
				zap.Duration("backoff", backoff))
			select {
			case <-time.After(backoff):
				continue
			case <-ctx.Done():
				return consumeRetryLater, ctx.Err()
			}
		}

		if !IsRetryableError(err) {
			if durableErr := c.handleTerminalFailure(ctx, msg, err, retryCount); durableErr != nil {
				return consumeRetryLater, durableErr
			}
			return consumeCommit, err
		}

		if retryCount < c.retryConfig.MaxRetries {
			backoff := c.retryConfig.CalculateBackoff(retryCount + 1)
			internal.Logger.Warn("[FileUploadConsumer]retrying scan after backoff",
				zap.Error(err),
				zap.Int("retry_count", retryCount+1),
				zap.Duration("backoff", backoff),
				zap.Int64("offset", msg.Offset))

			select {
			case <-time.After(backoff):
			case <-ctx.Done():
				return consumeRetryLater, ctx.Err()
			}
		}

		retryCount++
	}

	if durableErr := c.handleTerminalFailure(ctx, msg, lastErr, retryCount); durableErr != nil {
		return consumeRetryLater, durableErr
	}
	return consumeCommit, lastErr
}

func (c *FileUploadConsumer) calculateInboxLockBackoff(attempt int) time.Duration {
	backoff := c.retryConfig.CalculateBackoff(attempt)
	if backoff > 5*time.Second {
		return 5 * time.Second
	}
	return backoff
}

func (c *FileUploadConsumer) handleTerminalFailure(ctx context.Context, msg *sarama.ConsumerMessage, err error, retryCount int) error {
	if markErr := c.markMessageScanFailed(ctx, msg, err.Error()); markErr != nil {
		if errors.Is(markErr, errFileScanAlreadyConverged) {
			eventID, eventErr := extractEventID(msg.Value)
			if eventErr != nil || eventID == "" {
				return nil
			}
			return c.markInboxDone(ctx, eventID)
		}
		internal.Logger.Error("[FileUploadConsumer]mark scan_failed failed",
			zap.Error(markErr),
			zap.Int64("offset", msg.Offset))
	}

	if c.dlqProducer != nil {
		if dlqErr := c.dlqProducer.SendToDLQ(ctx, msg, err, retryCount); dlqErr != nil {
			internal.Logger.Error("[FileUploadConsumer]send to DLQ failed",
				zap.Error(dlqErr),
				zap.Int64("offset", msg.Offset))
		} else {
			return nil
		}
	}

	dlqMsg, buildErr := buildDLQMessage(msg, err, retryCount)
	if buildErr != nil {
		internal.Logger.Error("[FileUploadConsumer]build fallback DLQ record failed",
			zap.Error(buildErr),
			zap.Int64("offset", msg.Offset))
		return fmt.Errorf("terminal failure persisted incompletely: %w", buildErr)
	}

	if c.dlqProducer != nil {
		if persistErr := c.dlqProducer.persistToDB(ctx, dlqMsg); persistErr != nil {
			internal.Logger.Error("[FileUploadConsumer]persist fallback DLQ record failed",
				zap.Error(persistErr),
				zap.String("event_id", dlqMsg.EventID),
				zap.Int64("offset", msg.Offset))
			return fmt.Errorf("terminal failure persisted incompletely: %w", persistErr)
		}
		return nil
	}

	return fmt.Errorf("terminal failure persisted incompletely: dlq producer unavailable")
}

func (c *FileUploadConsumer) processMessage(ctx context.Context, msg *sarama.ConsumerMessage) error {
	return c.processMessageInternal(ctx, msg, false)
}

func (c *FileUploadConsumer) processDLQMessage(ctx context.Context, msg *sarama.ConsumerMessage) error {
	return c.processMessageInternal(ctx, msg, true)
}

func (c *FileUploadConsumer) processMessageInternal(ctx context.Context, msg *sarama.ConsumerMessage, allowDLQRetry bool) error {
	rid := extractKafkaRequestID(msg)
	l := internal.Logger
	if rid != "" {
		l = internal.Logger.With(zap.String("request_id", rid))
	}

	var payload model.FileEventPayload
	if err := json.Unmarshal(msg.Value, &payload); err != nil {
		return fmt.Errorf("%w: unmarshal payload: %v", ErrInvalidPayload, err)
	}
	payload.EventID = strings.TrimSpace(payload.EventID)
	payload.Sha1 = strings.TrimSpace(payload.Sha1)
	payload.OssKey = normalizeObjectKey(strings.TrimSpace(payload.OssKey), payload.Sha1)
	payload.EventType = strings.TrimSpace(payload.EventType)

	if payload.EventID == "" {
		return fmt.Errorf("%w: missing event_id", ErrInvalidEventID)
	}
	if payload.EventType == model.ObjectDeleteRequested {
		return c.processObjectDeleteRequested(ctx, &payload)
	}
	if payload.EventType != model.FileScanRequested {
		return fmt.Errorf("%w: unexpected event_type=%s", ErrInvalidPayload, payload.EventType)
	}
	if payload.Sha1 == "" && payload.FileID == 0 {
		return fmt.Errorf("%w: missing file identity", ErrInvalidPayload)
	}
	if payload.OssKey == "" {
		return fmt.Errorf("%w: missing object key", ErrInvalidObjectKey)
	}

	acquired, done, err := c.tryBeginInbox(ctx, payload.EventID, allowDLQRetry)
	if err != nil {
		return err
	}
	if done {
		return nil
	}
	if !acquired {
		return ErrInboxLocked
	}

	file, err := c.loadFileRecord(ctx, &payload)
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return fmt.Errorf("%w: file metadata missing", ErrFileNotFound)
		}
		return err
	}
	if file.Status == model.FileSuccess || file.Status == model.FileInfected {
		return c.markInboxDone(ctx, payload.EventID)
	}
	if file.ObjectKey != "" {
		payload.OssKey = file.ObjectKey
	}

	signature, infected, err := c.scanObject(ctx, payload.OssKey, payload.Size)
	if err != nil {
		l.Warn("[FileUploadConsumer]scan object failed",
			zap.String("event_id", payload.EventID),
			zap.String("object_key", payload.OssKey),
			zap.Error(err))
		return err
	}

	if infected {
		return c.finalizeInfected(ctx, &payload, signature)
	}
	return c.finalizeClean(ctx, &payload)
}

func (c *FileUploadConsumer) processObjectDeleteRequested(ctx context.Context, payload *model.FileEventPayload) error {
	if payload.EventID == "" {
		return fmt.Errorf("%w: missing event_id", ErrInvalidEventID)
	}
	if payload.FileID == 0 && payload.Sha1 == "" {
		return fmt.Errorf("%w: missing file identity", ErrInvalidPayload)
	}
	if payload.OssKey == "" {
		payload.OssKey = normalizeObjectKey(payload.OssKey, payload.Sha1)
	}
	if payload.OssKey == "" {
		return fmt.Errorf("%w: missing object key", ErrInvalidObjectKey)
	}

	acquired, done, err := c.tryBeginInbox(ctx, payload.EventID, false)
	if err != nil {
		return err
	}
	if done {
		return nil
	}
	if !acquired {
		return ErrInboxLocked
	}

	var remaining int64
	query := internal.DB.WithContext(ctx).Model(&model.UserFile{})
	if payload.FileID != 0 {
		query = query.Where("file_id = ?", payload.FileID)
	} else {
		query = query.Joins("JOIN files ON files.id = user_files.file_id AND files.sha1 = ?", payload.Sha1)
	}
	if err := query.Count(&remaining).Error; err != nil {
		return err
	}
	if remaining > 0 {
		internal.Logger.Warn("[FileUploadConsumer]skip object delete because references remain",
			zap.String("event_id", payload.EventID),
			zap.Int64("remaining", remaining))
		return c.markInboxDone(ctx, payload.EventID)
	}

	if c.ossClient == nil {
		return fmt.Errorf("%w: minio client not initialized", ErrServiceUnavailable)
	}
	if err := c.ossClient.MinIODeleteObject(payload.OssKey); err != nil {
		resp := minio.ToErrorResponse(err)
		if resp.Code != "NoSuchKey" && resp.Code != "NotFound" {
			return fmt.Errorf("%w: delete object: %v", ErrTemporaryFailure, err)
		}
	}

	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if payload.FileID != 0 {
			if err := tx.Where("id = ?", payload.FileID).Delete(&model.File{}).Error; err != nil {
				return err
			}
		} else if payload.Sha1 != "" {
			if err := tx.Where("sha1 = ?", payload.Sha1).Delete(&model.File{}).Error; err != nil {
				return err
			}
		}
		return c.markInboxDoneTx(tx, payload.EventID)
	})
}

func (c *FileUploadConsumer) loadFileRecord(ctx context.Context, payload *model.FileEventPayload) (*model.File, error) {
	var file model.File
	query := internal.DB.WithContext(ctx)
	switch {
	case payload.FileID != 0:
		err := query.Take(&file, payload.FileID).Error
		return &file, err
	case payload.Sha1 != "":
		err := query.Where("sha1 = ?", payload.Sha1).Take(&file).Error
		return &file, err
	default:
		return nil, gorm.ErrRecordNotFound
	}
}

func (c *FileUploadConsumer) scanObject(ctx context.Context, objectKey string, objectSize int64) (signature string, infected bool, err error) {
	if c.ossClient == nil {
		return "", false, fmt.Errorf("%w: minio client not initialized", ErrServiceUnavailable)
	}

	timeout := internal.ViperConf.ClamAV.Timeout()
	scanCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	reader, err := c.ossClient.MinIOGetObjectStream(scanCtx, objectKey)
	if err != nil {
		return "", false, fmt.Errorf("%w: get object stream: %v", ErrTemporaryFailure, err)
	}
	defer reader.Close()

	verdict, err := ScanReaderWithClamAV(scanCtx, reader, objectSize)
	if err != nil {
		return "", false, err
	}
	return verdict.Signature, verdict.Infected, nil
}

func (c *FileUploadConsumer) finalizeClean(ctx context.Context, payload *model.FileEventPayload) error {
	now := time.Now()
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if err := c.updateFileByEvent(tx, payload, map[string]any{
			"status":       model.FileSuccess,
			"scan_detail":  "",
			"scanned_at":   &now,
			"object_key":   payload.OssKey,
			"content_type": payload.ContentType,
			"size":         payload.Size,
		}); err != nil {
			if errors.Is(err, errFileScanAlreadyConverged) {
				return c.markInboxDoneTx(tx, payload.EventID)
			}
			return err
		}
		return c.markInboxDoneTx(tx, payload.EventID)
	})
}

func (c *FileUploadConsumer) finalizeInfected(ctx context.Context, payload *model.FileEventPayload, signature string) error {
	if c.ossClient == nil {
		return fmt.Errorf("%w: minio client not initialized", ErrServiceUnavailable)
	}

	if err := c.ossClient.MinIODeleteObject(payload.OssKey); err != nil {
		resp := minio.ToErrorResponse(err)
		if resp.Code != "NoSuchKey" && resp.Code != "NotFound" {
			return fmt.Errorf("%w: delete infected object: %v", ErrTemporaryFailure, err)
		}
	}

	now := time.Now()
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if err := c.updateFileByEvent(tx, payload, map[string]any{
			"status":       model.FileInfected,
			"scan_detail":  truncateReason(signature),
			"scanned_at":   &now,
			"object_key":   payload.OssKey,
			"content_type": payload.ContentType,
			"size":         payload.Size,
		}); err != nil {
			if errors.Is(err, errFileScanAlreadyConverged) {
				return c.markInboxDoneTx(tx, payload.EventID)
			}
			return err
		}
		return c.markInboxDoneTx(tx, payload.EventID)
	})
}

func (c *FileUploadConsumer) markMessageScanFailed(ctx context.Context, msg *sarama.ConsumerMessage, reason string) error {
	var payload model.FileEventPayload
	if err := json.Unmarshal(msg.Value, &payload); err != nil {
		return err
	}
	return c.markFileScanFailed(ctx, &payload, reason)
}

func (c *FileUploadConsumer) markFileScanFailed(ctx context.Context, payload *model.FileEventPayload, reason string) error {
	now := time.Now()
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if err := c.updateFileByEvent(tx, payload, map[string]any{
			"status":      model.FileScanFailed,
			"scan_detail": truncateReason(reason),
			"scanned_at":  &now,
		}); err != nil {
			if errors.Is(err, errFileScanAlreadyConverged) {
				return err
			}
			if errors.Is(err, gorm.ErrRecordNotFound) {
				return nil
			}
			return err
		}
		return nil
	})
}

func (c *FileUploadConsumer) updateFileByEvent(tx *gorm.DB, payload *model.FileEventPayload, updates map[string]any) error {
	if payload == nil {
		return fmt.Errorf("%w: nil payload", ErrInvalidPayload)
	}

	db := tx.Model(&model.File{})
	switch {
	case payload.FileID != 0:
		db = db.Where("id = ?", payload.FileID)
	case payload.Sha1 != "":
		db = db.Where("sha1 = ?", payload.Sha1)
	default:
		return gorm.ErrRecordNotFound
	}

	result := db.Where("status = ?", model.FilePendingScan).Updates(updates)
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected == 0 {
		converged, err := c.fileScanAlreadyConvergedTx(tx, payload)
		if err != nil {
			return err
		}
		if converged {
			return errFileScanAlreadyConverged
		}
		return gorm.ErrRecordNotFound
	}
	return nil
}

func (c *FileUploadConsumer) fileScanAlreadyConvergedTx(tx *gorm.DB, payload *model.FileEventPayload) (bool, error) {
	if payload == nil {
		return false, fmt.Errorf("%w: nil payload", ErrInvalidPayload)
	}

	var file model.File
	db := tx.Select("status")
	switch {
	case payload.FileID != 0:
		db = db.Where("id = ?", payload.FileID)
	case payload.Sha1 != "":
		db = db.Where("sha1 = ?", payload.Sha1)
	default:
		return false, gorm.ErrRecordNotFound
	}
	if err := db.Take(&file).Error; err != nil {
		return false, err
	}

	switch file.Status {
	case model.FileSuccess, model.FileInfected, model.FileScanFailed:
		return true, nil
	default:
		return false, nil
	}
}

func (c *FileUploadConsumer) markInboxDone(ctx context.Context, eventID string) error {
	return internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		return c.markInboxDoneTx(tx, eventID)
	})
}

func (c *FileUploadConsumer) markInboxDoneTx(tx *gorm.DB, eventID string) error {
	now := time.Now()
	result := tx.Model(&model.Inbox{}).
		Where("event_id = ? AND locked_by = ?", eventID, c.instanceID).
		Updates(map[string]any{
			"status":       model.InboxDone,
			"locked_by":    "",
			"locked_until": nil,
			"last_error":   "",
			"updated_at":   now,
		})
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected != 1 {
		return fmt.Errorf("inbox event %q was not marked done by lease owner %q", eventID, c.instanceID)
	}
	return nil
}

func (c *FileUploadConsumer) tryBeginInbox(ctx context.Context, eventID string, allowDLQRetry bool) (acquired bool, done bool, err error) {
	now := time.Now()
	lockUntil := now.Add(2 * time.Minute)

	rec := model.Inbox{
		EventID:     eventID,
		Status:      model.InboxProcessing,
		LockedBy:    c.instanceID,
		LockedUntil: &lockUntil,
		Attempts:    1,
	}

	if err := internal.DB.WithContext(ctx).Create(&rec).Error; err == nil {
		return true, false, nil
	} else if !isDupKey(err) {
		return false, false, err
	}

	var existing model.Inbox
	if err := internal.DB.WithContext(ctx).First(&existing, "event_id = ?", eventID).Error; err != nil {
		return false, false, err
	}
	if existing.Status == model.InboxDone {
		return false, true, nil
	}
	if existing.Status == model.InboxDLQ {
		if !allowDLQRetry {
			return false, true, nil
		}
		res := internal.DB.WithContext(ctx).Model(&model.Inbox{}).
			Where("event_id = ? AND status = ?", eventID, model.InboxDLQ).
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
		if res.RowsAffected > 0 {
			return true, false, nil
		}
	}
	if existing.Status == model.InboxProcessing && existing.LockedBy == c.instanceID {
		res := internal.DB.WithContext(ctx).Model(&model.Inbox{}).
			Where("event_id = ? AND status = ? AND locked_by = ?", eventID, model.InboxProcessing, c.instanceID).
			Updates(map[string]any{
				"locked_until": &lockUntil,
				"attempts":     gorm.Expr("attempts + 1"),
				"updated_at":   now,
			})
		if res.Error != nil {
			return false, false, res.Error
		}
		if res.RowsAffected > 0 {
			return true, false, nil
		}
	}

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
		return false, false, nil
	}
	return true, false, nil
}

func (c *FileUploadConsumer) markInboxDLQ(ctx context.Context, eventID, errMsg string) error {
	now := time.Now()
	result := internal.DB.WithContext(ctx).Model(&model.Inbox{}).
		Where("event_id = ? AND locked_by = ?", eventID, c.instanceID).
		Updates(map[string]any{
			"status":       model.InboxDLQ,
			"last_error":   truncateReason(errMsg),
			"locked_until": nil,
			"locked_by":    "",
			"updated_at":   now,
		})
	if result.Error != nil {
		return result.Error
	}
	if result.RowsAffected != 1 {
		return fmt.Errorf("inbox event %q was not marked DLQ by lease owner %q", eventID, c.instanceID)
	}
	return nil
}

func isDupKey(err error) bool {
	if err == nil {
		return false
	}
	s := err.Error()
	return strings.Contains(s, "Duplicate entry") || strings.Contains(s, "Error 1062")
}

func extractKafkaRequestID(msg *sarama.ConsumerMessage) string {
	for _, h := range msg.Headers {
		if h != nil && string(h.Key) == "x-request-id" {
			return string(h.Value)
		}
	}
	return ""
}

func normalizeObjectKey(objectKey, sha1 string) string {
	if objectKey != "" {
		return objectKey
	}
	if sha1 == "" {
		return ""
	}
	return "files/" + sha1
}

func truncateReason(reason string) string {
	const maxLen = 1024
	reason = strings.TrimSpace(reason)
	if len(reason) <= maxLen {
		return reason
	}
	return reason[:maxLen]
}

func osHostname() (string, error) {
	return os.Hostname()
}

func osPID() int {
	return os.Getpid()
}

func extractEventID(raw []byte) (string, error) {
	var payload model.FileEventPayload
	if err := json.Unmarshal(raw, &payload); err != nil {
		return "", err
	}
	return strings.TrimSpace(payload.EventID), nil
}
