package middleware

import (
	"context"
	"go_test/backword_part/mcp_server/apikey"
	"go_test/backword_part/model"
	"net/http"
	"strings"
	"time"

	"go.uber.org/zap"
)

const (
	// HTTP Header 名称
	APIKeyHeader = "X-API-Key"

	// Context Key
	APIKeyContextKey = "api_key"
	UserIDContextKey = "user_id"
)

// APIKeyAuth API Key 认证中间件
type APIKeyAuth struct {
	manager     *apikey.Manager
	rateLimiter *apikey.RateLimiter
	logger      *zap.Logger
}

// NewAPIKeyAuth 创建认证中间件
func NewAPIKeyAuth(manager *apikey.Manager, rateLimiter *apikey.RateLimiter, logger *zap.Logger) *APIKeyAuth {
	return &APIKeyAuth{
		manager:     manager,
		rateLimiter: rateLimiter,
		logger:      logger,
	}
}

// Middleware HTTP 中间件
func (a *APIKeyAuth) Middleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ctx := r.Context()

		// 1. 提取 API Key
		apiKey := extractAPIKey(r)
		if apiKey == "" {
			a.respondError(w, r, http.StatusUnauthorized, "Missing API key")
			return
		}

		// 2. 验证 API Key
		key, err := a.manager.ValidateAPIKey(ctx, apiKey)
		if err != nil {
			a.respondError(w, r, http.StatusUnauthorized, err.Error())
			a.logUsage(ctx, 0, 0, r, http.StatusUnauthorized, err.Error())
			return
		}

		// 3. 检查 Rate Limit
		allowed, err := a.rateLimiter.CheckRateLimit(ctx, key.ID)
		if err != nil {
			a.logger.Error("Rate limit check failed", zap.Error(err))
			a.respondError(w, r, http.StatusInternalServerError, "Internal server error")
			return
		}

		if !allowed {
			a.respondError(w, r, http.StatusTooManyRequests, "Rate limit exceeded")
			a.logUsage(ctx, key.ID, key.UserID, r, http.StatusTooManyRequests, "Rate limit exceeded")
			return
		}

		// 4. 将 API Key 信息存入 Context
		ctx = context.WithValue(ctx, APIKeyContextKey, key)
		ctx = context.WithValue(ctx, UserIDContextKey, key.UserID)

		// 5. 记录成功的请求
		go a.logUsage(ctx, key.ID, key.UserID, r, http.StatusOK, "")

		// 6. 继续处理请求
		next.ServeHTTP(w, r.WithContext(ctx))
	})
}

// extractAPIKey 从请求中提取 API Key
func extractAPIKey(r *http.Request) string {
	// 1. 从 Header 中提取
	apiKey := r.Header.Get(APIKeyHeader)
	if apiKey != "" {
		return apiKey
	}

	// 2. 从 Authorization Header 中提取（Bearer token）
	auth := r.Header.Get("Authorization")
	if strings.HasPrefix(auth, "Bearer ") {
		return strings.TrimPrefix(auth, "Bearer ")
	}

	// 3. 从 Query 参数中提取（不推荐，但支持）
	return r.URL.Query().Get("api_key")
}

// respondError 返回错误响应
func (a *APIKeyAuth) respondError(w http.ResponseWriter, r *http.Request, statusCode int, message string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	w.Write([]byte(`{"error":"` + message + `"}`))
}

// logUsage 记录 API Key 使用日志
func (a *APIKeyAuth) logUsage(ctx context.Context, apiKeyID, userID uint, r *http.Request, statusCode int, errorMsg string) {
	log := &model.MCPAPIKeyLog{
		APIKeyID:     apiKeyID,
		UserID:       userID,
		Endpoint:     r.URL.Path,
		Method:       r.Method,
		IPAddress:    getClientIP(r),
		UserAgent:    r.UserAgent(),
		StatusCode:   statusCode,
		ErrorMessage: errorMsg,
		RequestTime:  time.Now(),
	}

	if err := a.manager.LogAPIKeyUsage(ctx, log); err != nil {
		a.logger.Error("Failed to log API key usage", zap.Error(err))
	}
}

// getClientIP 获取客户端 IP
func getClientIP(r *http.Request) string {
	// 1. 从 X-Forwarded-For 获取
	if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
		ips := strings.Split(xff, ",")
		return strings.TrimSpace(ips[0])
	}

	// 2. 从 X-Real-IP 获取
	if xri := r.Header.Get("X-Real-IP"); xri != "" {
		return xri
	}

	// 3. 从 RemoteAddr 获取
	ip := r.RemoteAddr
	if idx := strings.LastIndex(ip, ":"); idx != -1 {
		ip = ip[:idx]
	}

	return ip
}

// GetAPIKeyFromContext 从 Context 中获取 API Key
func GetAPIKeyFromContext(ctx context.Context) *model.MCPAPIKey {
	if key, ok := ctx.Value(APIKeyContextKey).(*model.MCPAPIKey); ok {
		return key
	}
	return nil
}

// GetUserIDFromContext 从 Context 中获取 User ID
func GetUserIDFromContext(ctx context.Context) uint {
	if userID, ok := ctx.Value(UserIDContextKey).(uint); ok {
		return userID
	}
	return 0
}
