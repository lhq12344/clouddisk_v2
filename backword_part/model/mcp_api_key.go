package model

import (
	"time"
)

// MCPAPIKeyStatus API Key 状态
type MCPAPIKeyStatus string

const (
	MCPAPIKeyActive  MCPAPIKeyStatus = "active"
	MCPAPIKeyRevoked MCPAPIKeyStatus = "revoked"
	MCPAPIKeyExpired MCPAPIKeyStatus = "expired"
)

// MCPAPIKey MCP API Key 模型
type MCPAPIKey struct {
	ID         uint            `gorm:"primaryKey;autoIncrement" json:"id"`
	UserID     uint            `gorm:"not null;index:idx_user_status" json:"user_id"`
	KeyHash    string          `gorm:"type:varchar(64);not null;uniqueIndex" json:"-"`
	KeyPrefix  string          `gorm:"type:varchar(16);not null" json:"key_prefix"`
	Name       string          `gorm:"type:varchar(100);not null" json:"name"`
	Status     MCPAPIKeyStatus `gorm:"type:varchar(20);not null;default:'active';index:idx_user_status" json:"status"`
	ExpiresAt  *time.Time      `json:"expires_at"`
	LastUsedAt *time.Time      `json:"last_used_at"`
	CreatedAt  time.Time       `gorm:"autoCreateTime" json:"created_at"`
	UpdatedAt  time.Time       `gorm:"autoUpdateTime" json:"updated_at"`
}

// TableName 指定表名
func (MCPAPIKey) TableName() string {
	return "mcp_api_keys"
}

// IsExpired 检查是否过期
func (k *MCPAPIKey) IsExpired() bool {
	if k.ExpiresAt == nil {
		return false
	}
	return time.Now().After(*k.ExpiresAt)
}

// MCPAPIKeyLog API Key 使用日志
type MCPAPIKeyLog struct {
	ID           uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	APIKeyID     uint      `gorm:"not null;index" json:"api_key_id"`
	UserID       uint      `gorm:"not null;index" json:"user_id"`
	Endpoint     string    `gorm:"type:varchar(255);not null" json:"endpoint"`
	Method       string    `gorm:"type:varchar(10);not null" json:"method"`
	IPAddress    string    `gorm:"type:varchar(45)" json:"ip_address"`
	UserAgent    string    `gorm:"type:varchar(255)" json:"user_agent"`
	StatusCode   int       `gorm:"not null" json:"status_code"`
	ErrorMessage string    `gorm:"type:text" json:"error_message,omitempty"`
	RequestTime  time.Time `gorm:"not null;index" json:"request_time"`
}

// TableName 指定表名
func (MCPAPIKeyLog) TableName() string {
	return "mcp_api_key_logs"
}

// MCPRateLimit Rate Limit 记录
type MCPRateLimit struct {
	ID           uint      `gorm:"primaryKey;autoIncrement" json:"id"`
	APIKeyID     uint      `gorm:"not null;uniqueIndex:idx_key_window" json:"api_key_id"`
	WindowStart  time.Time `gorm:"not null;uniqueIndex:idx_key_window;index" json:"window_start"`
	RequestCount int       `gorm:"not null;default:0" json:"request_count"`
	CreatedAt    time.Time `gorm:"autoCreateTime" json:"created_at"`
	UpdatedAt    time.Time `gorm:"autoUpdateTime" json:"updated_at"`
}

// TableName 指定表名
func (MCPRateLimit) TableName() string {
	return "mcp_rate_limits"
}
