package apikey

import (
	"context"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"time"

	"go.uber.org/zap"
	"gorm.io/gorm"
)

const (
	// 默认限流：每分钟 60 次请求
	DefaultRateLimit = 60
	WindowDuration   = 1 * time.Minute
)

// RateLimiter Rate Limiter
type RateLimiter struct {
	db     *gorm.DB
	logger *zap.Logger
	limit  int
}

// NewRateLimiter 创建 Rate Limiter
func NewRateLimiter(limit int) *RateLimiter {
	if limit <= 0 {
		limit = DefaultRateLimit
	}

	return &RateLimiter{
		db:     internal.DB,
		logger: internal.Logger,
		limit:  limit,
	}
}

// CheckRateLimit 检查是否超过限流
func (r *RateLimiter) CheckRateLimit(ctx context.Context, apiKeyID uint) (bool, error) {
	// 1. 计算当前时间窗口
	now := time.Now()
	windowStart := now.Truncate(WindowDuration)

	// 2. 查询或创建当前窗口的记录
	var rateLimit model.MCPRateLimit
	err := r.db.WithContext(ctx).
		Where("api_key_id = ? AND window_start = ?", apiKeyID, windowStart).
		First(&rateLimit).Error

	if err != nil {
		if err == gorm.ErrRecordNotFound {
			// 创建新记录
			rateLimit = model.MCPRateLimit{
				APIKeyID:     apiKeyID,
				WindowStart:  windowStart,
				RequestCount: 1,
			}
			if err := r.db.Create(&rateLimit).Error; err != nil {
				return false, fmt.Errorf("failed to create rate limit record: %w", err)
			}
			return true, nil // 第一次请求，允许
		}
		return false, fmt.Errorf("database error: %w", err)
	}

	// 3. 检查是否超过限制
	if rateLimit.RequestCount >= r.limit {
		r.logger.Warn("Rate limit exceeded",
			zap.Uint("api_key_id", apiKeyID),
			zap.Int("request_count", rateLimit.RequestCount),
			zap.Int("limit", r.limit))
		return false, nil
	}

	// 4. 增加计数
	result := r.db.Model(&rateLimit).
		Where("id = ? AND request_count < ?", rateLimit.ID, r.limit).
		Update("request_count", gorm.Expr("request_count + 1"))

	if result.Error != nil {
		return false, fmt.Errorf("failed to update rate limit: %w", result.Error)
	}

	if result.RowsAffected == 0 {
		// 并发情况下可能已经超过限制
		return false, nil
	}

	return true, nil
}

// CleanupOldRecords 清理旧的限流记录
func (r *RateLimiter) CleanupOldRecords(ctx context.Context, retentionHours int) (int64, error) {
	cutoffTime := time.Now().Add(-time.Duration(retentionHours) * time.Hour)

	result := r.db.WithContext(ctx).
		Where("window_start < ?", cutoffTime).
		Delete(&model.MCPRateLimit{})

	if result.Error != nil {
		return 0, result.Error
	}

	r.logger.Info("Cleaned up old rate limit records",
		zap.Int64("deleted_count", result.RowsAffected))

	return result.RowsAffected, nil
}
