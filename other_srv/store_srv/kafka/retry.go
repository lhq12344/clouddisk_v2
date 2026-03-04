package kafka

import (
	"errors"
	"math"
	"time"
)

// RetryConfig 重试配置
type RetryConfig struct {
	MaxRetries        int           // 最大重试次数
	InitialBackoff    time.Duration // 初始退避时间
	MaxBackoff        time.Duration // 最大退避时间
	BackoffMultiplier float64       // 退避倍数
}

// DefaultRetryConfig 默认重试配置
func DefaultRetryConfig() RetryConfig {
	return RetryConfig{
		MaxRetries:        3,
		InitialBackoff:    1 * time.Second,
		MaxBackoff:        30 * time.Second,
		BackoffMultiplier: 2.0,
	}
}

// CalculateBackoff 计算退避时间（指数退避）
func (c RetryConfig) CalculateBackoff(attempt int) time.Duration {
	if attempt <= 0 {
		return c.InitialBackoff
	}

	backoff := float64(c.InitialBackoff) * math.Pow(c.BackoffMultiplier, float64(attempt-1))
	if backoff > float64(c.MaxBackoff) {
		backoff = float64(c.MaxBackoff)
	}

	return time.Duration(backoff)
}

// 错误类型定义
var (
	// 可重试错误（临时性故障）
	ErrNetworkTimeout       = errors.New("network timeout")
	ErrServiceUnavailable   = errors.New("service unavailable")
	ErrRateLimitExceeded    = errors.New("rate limit exceeded")
	ErrConnectionReset      = errors.New("connection reset")
	ErrTemporaryFailure     = errors.New("temporary failure")
	ErrOSSConnectionTimeout = errors.New("oss connection timeout")

	// 不可重试错误（永久性故障）
	ErrInvalidFileFormat     = errors.New("invalid file format")
	ErrFileNotFound          = errors.New("file not found")
	ErrAuthenticationFailed  = errors.New("authentication failed")
	ErrInvalidObjectKey      = errors.New("invalid object key")
	ErrFileTooLarge          = errors.New("file too large")
	ErrInvalidPayload        = errors.New("invalid payload")
	ErrDuplicateEvent        = errors.New("duplicate event")
	ErrInvalidEventID        = errors.New("invalid event id")
)

// IsRetryableError 判断错误是否可重试
func IsRetryableError(err error) bool {
	if err == nil {
		return false
	}

	// 可重试错误列表
	retryableErrors := []error{
		ErrNetworkTimeout,
		ErrServiceUnavailable,
		ErrRateLimitExceeded,
		ErrConnectionReset,
		ErrTemporaryFailure,
		ErrOSSConnectionTimeout,
	}

	for _, retryable := range retryableErrors {
		if errors.Is(err, retryable) {
			return true
		}
	}

	// 检查错误消息中的关键字
	errMsg := err.Error()
	retryableKeywords := []string{
		"timeout",
		"connection refused",
		"connection reset",
		"temporary failure",
		"service unavailable",
		"rate limit",
		"too many requests",
	}

	for _, keyword := range retryableKeywords {
		if contains(errMsg, keyword) {
			return true
		}
	}

	return false
}

// contains 检查字符串是否包含子串（不区分大小写）
func contains(s, substr string) bool {
	return len(s) >= len(substr) &&
		(s == substr || len(s) > len(substr) &&
		(s[:len(substr)] == substr || s[len(s)-len(substr):] == substr ||
		len(s) > len(substr)*2))
}

// ErrorCategory 错误分类
type ErrorCategory string

const (
	ErrorCategoryRetryable    ErrorCategory = "retryable"    // 可重试
	ErrorCategoryNonRetryable ErrorCategory = "non_retryable" // 不可重试
	ErrorCategoryUnknown      ErrorCategory = "unknown"      // 未知
)

// CategorizeError 对错误进行分类
func CategorizeError(err error) ErrorCategory {
	if err == nil {
		return ErrorCategoryUnknown
	}

	if IsRetryableError(err) {
		return ErrorCategoryRetryable
	}

	// 检查不可重试错误
	nonRetryableErrors := []error{
		ErrInvalidFileFormat,
		ErrFileNotFound,
		ErrAuthenticationFailed,
		ErrInvalidObjectKey,
		ErrFileTooLarge,
		ErrInvalidPayload,
		ErrDuplicateEvent,
		ErrInvalidEventID,
	}

	for _, nonRetryable := range nonRetryableErrors {
		if errors.Is(err, nonRetryable) {
			return ErrorCategoryNonRetryable
		}
	}

	return ErrorCategoryUnknown
}
