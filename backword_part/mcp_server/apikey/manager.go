package apikey

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"time"

	"go.uber.org/zap"
	"gorm.io/gorm"
)

const (
	// API Key 格式: mcp_<32字符随机串>
	APIKeyPrefix = "mcp_"
	APIKeyLength = 32

	// 默认过期时间：1年
	DefaultExpiryDays = 365
)

// Manager API Key 管理器
type Manager struct {
	db     *gorm.DB
	logger *zap.Logger
}

// NewManager 创建 API Key 管理器
func NewManager() *Manager {
	return &Manager{
		db:     internal.DB,
		logger: internal.Logger,
	}
}

// GenerateAPIKey 生成新的 API Key
func (m *Manager) GenerateAPIKey(ctx context.Context, userID uint, name string, expiryDays int) (string, error) {
	if expiryDays <= 0 {
		expiryDays = DefaultExpiryDays
	}

	// 1. 生成随机 API Key
	apiKey, err := generateRandomKey()
	if err != nil {
		return "", fmt.Errorf("failed to generate random key: %w", err)
	}

	// 2. 计算哈希值
	keyHash := hashAPIKey(apiKey)
	keyPrefix := apiKey[:len(APIKeyPrefix)+8] // mcp_xxxxxxxx

	// 3. 计算过期时间
	expiresAt := time.Now().AddDate(0, 0, expiryDays)

	// 4. 在事务中：撤销旧 Key + 创建新 Key
	err = m.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 撤销该用户的所有旧 Key
		result := tx.Model(&model.MCPAPIKey{}).
			Where("user_id = ? AND status = ?", userID, model.MCPAPIKeyActive).
			Updates(map[string]interface{}{
				"status":     model.MCPAPIKeyRevoked,
				"updated_at": time.Now(),
			})

		if result.Error != nil {
			return fmt.Errorf("failed to revoke old keys: %w", result.Error)
		}

		if result.RowsAffected > 0 {
			m.logger.Info("Revoked old API keys",
				zap.Uint("user_id", userID),
				zap.Int64("count", result.RowsAffected))
		}

		// 创建新 Key
		newKey := model.MCPAPIKey{
			UserID:    userID,
			KeyHash:   keyHash,
			KeyPrefix: keyPrefix,
			Name:      name,
			Status:    model.MCPAPIKeyActive,
			ExpiresAt: &expiresAt,
		}

		if err := tx.Create(&newKey).Error; err != nil {
			return fmt.Errorf("failed to create new key: %w", err)
		}

		m.logger.Info("Created new API key",
			zap.Uint("user_id", userID),
			zap.Uint("key_id", newKey.ID),
			zap.String("key_prefix", keyPrefix))

		return nil
	})

	if err != nil {
		return "", err
	}

	// 5. 返回明文 API Key（只在创建时返回一次）
	return apiKey, nil
}

// ValidateAPIKey 验证 API Key
func (m *Manager) ValidateAPIKey(ctx context.Context, apiKey string) (*model.MCPAPIKey, error) {
	// 1. 计算哈希值
	keyHash := hashAPIKey(apiKey)

	// 2. 查询数据库
	var key model.MCPAPIKey
	err := m.db.WithContext(ctx).
		Where("key_hash = ?", keyHash).
		First(&key).Error

	if err != nil {
		if err == gorm.ErrRecordNotFound {
			return nil, fmt.Errorf("invalid API key")
		}
		return nil, fmt.Errorf("database error: %w", err)
	}

	// 3. 检查状态
	if key.Status != model.MCPAPIKeyActive {
		return nil, fmt.Errorf("API key is %s", key.Status)
	}

	// 4. 检查过期
	if key.IsExpired() {
		// 自动标记为过期
		m.db.Model(&key).Update("status", model.MCPAPIKeyExpired)
		return nil, fmt.Errorf("API key has expired")
	}

	// 5. 更新最后使用时间（异步）
	go func() {
		now := time.Now()
		m.db.Model(&model.MCPAPIKey{}).
			Where("id = ?", key.ID).
			Update("last_used_at", &now)
	}()

	return &key, nil
}

// RevokeAPIKey 撤销 API Key
func (m *Manager) RevokeAPIKey(ctx context.Context, userID uint) error {
	result := m.db.WithContext(ctx).
		Model(&model.MCPAPIKey{}).
		Where("user_id = ? AND status = ?", userID, model.MCPAPIKeyActive).
		Update("status", model.MCPAPIKeyRevoked)

	if result.Error != nil {
		return fmt.Errorf("failed to revoke API key: %w", result.Error)
	}

	if result.RowsAffected == 0 {
		return fmt.Errorf("no active API key found")
	}

	m.logger.Info("Revoked API key", zap.Uint("user_id", userID))
	return nil
}

// GetUserAPIKey 获取用户的 API Key 信息（不包含完整 Key）
func (m *Manager) GetUserAPIKey(ctx context.Context, userID uint) (*model.MCPAPIKey, error) {
	var key model.MCPAPIKey
	err := m.db.WithContext(ctx).
		Where("user_id = ? AND status = ?", userID, model.MCPAPIKeyActive).
		First(&key).Error

	if err != nil {
		if err == gorm.ErrRecordNotFound {
			return nil, nil // 没有 API Key
		}
		return nil, fmt.Errorf("database error: %w", err)
	}

	return &key, nil
}

// LogAPIKeyUsage 记录 API Key 使用日志
func (m *Manager) LogAPIKeyUsage(ctx context.Context, log *model.MCPAPIKeyLog) error {
	return m.db.WithContext(ctx).Create(log).Error
}

// generateRandomKey 生成随机 API Key
func generateRandomKey() (string, error) {
	bytes := make([]byte, APIKeyLength)
	if _, err := rand.Read(bytes); err != nil {
		return "", err
	}

	// 使用 base64 编码（URL 安全）
	encoded := base64.RawURLEncoding.EncodeToString(bytes)

	// 截取到指定长度
	if len(encoded) > APIKeyLength {
		encoded = encoded[:APIKeyLength]
	}

	return APIKeyPrefix + encoded, nil
}

// hashAPIKey 计算 API Key 的哈希值
func hashAPIKey(apiKey string) string {
	hash := sha256.Sum256([]byte(apiKey))
	return hex.EncodeToString(hash[:])
}
