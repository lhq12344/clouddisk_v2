package handler

import (
	"encoding/json"
	"go_test/backword_part/mcp_server/apikey"
	"go_test/backword_part/mcp_server/middleware"
	"net/http"
	"strconv"

	"go.uber.org/zap"
)

// APIKeyHandler API Key 管理 Handler
type APIKeyHandler struct {
	manager *apikey.Manager
	logger  *zap.Logger
}

// NewAPIKeyHandler 创建 Handler
func NewAPIKeyHandler(manager *apikey.Manager, logger *zap.Logger) *APIKeyHandler {
	return &APIKeyHandler{
		manager: manager,
		logger:  logger,
	}
}

// GenerateAPIKeyRequest 生成 API Key 请求
type GenerateAPIKeyRequest struct {
	UserID     uint   `json:"user_id"`
	Name       string `json:"name"`
	ExpiryDays int    `json:"expiry_days"`
}

// GenerateAPIKeyResponse 生成 API Key 响应
type GenerateAPIKeyResponse struct {
	APIKey    string `json:"api_key"`
	KeyPrefix string `json:"key_prefix"`
	ExpiresAt string `json:"expires_at"`
	Message   string `json:"message"`
}

// HandleGenerateAPIKey 生成 API Key
func (h *APIKeyHandler) HandleGenerateAPIKey(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req GenerateAPIKeyRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		h.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.UserID == 0 {
		h.respondError(w, http.StatusBadRequest, "user_id is required")
		return
	}

	// 生成 API Key
	apiKey, err := h.manager.GenerateAPIKey(r.Context(), req.UserID, req.Name, req.ExpiryDays)
	if err != nil {
		h.logger.Error("Failed to generate API key", zap.Error(err))
		h.respondError(w, http.StatusInternalServerError, "Failed to generate API key")
		return
	}

	// 获取 Key 信息
	keyInfo, _ := h.manager.GetUserAPIKey(r.Context(), req.UserID)

	resp := GenerateAPIKeyResponse{
		APIKey:    apiKey,
		KeyPrefix: keyInfo.KeyPrefix,
		ExpiresAt: keyInfo.ExpiresAt.Format("2006-01-02 15:04:05"),
		Message:   "API key generated successfully. Please save it securely, it will not be shown again.",
	}

	h.respondJSON(w, http.StatusOK, resp)
}

// HandleGetAPIKey 获取 API Key 信息
func (h *APIKeyHandler) HandleGetAPIKey(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	userIDStr := r.URL.Query().Get("user_id")
	if userIDStr == "" {
		h.respondError(w, http.StatusBadRequest, "user_id is required")
		return
	}

	userID, err := strconv.ParseUint(userIDStr, 10, 32)
	if err != nil {
		h.respondError(w, http.StatusBadRequest, "Invalid user_id")
		return
	}

	keyInfo, err := h.manager.GetUserAPIKey(r.Context(), uint(userID))
	if err != nil {
		h.logger.Error("Failed to get API key", zap.Error(err))
		h.respondError(w, http.StatusInternalServerError, "Failed to get API key")
		return
	}

	if keyInfo == nil {
		h.respondError(w, http.StatusNotFound, "No active API key found")
		return
	}

	h.respondJSON(w, http.StatusOK, keyInfo)
}

// HandleRevokeAPIKey 撤销 API Key
func (h *APIKeyHandler) HandleRevokeAPIKey(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var req struct {
		UserID uint `json:"user_id"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		h.respondError(w, http.StatusBadRequest, "Invalid request body")
		return
	}

	if req.UserID == 0 {
		h.respondError(w, http.StatusBadRequest, "user_id is required")
		return
	}

	// 撤销 API Key
	if err := h.manager.RevokeAPIKey(r.Context(), req.UserID); err != nil {
		h.logger.Error("Failed to revoke API key", zap.Error(err))
		h.respondError(w, http.StatusInternalServerError, err.Error())
		return
	}

	h.respondJSON(w, http.StatusOK, map[string]string{
		"message": "API key revoked successfully",
	})
}

// HandleGetCurrentUser 获取当前认证用户信息（需要 API Key）
func (h *APIKeyHandler) HandleGetCurrentUser(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	// 从 Context 中获取 API Key 信息
	apiKey := middleware.GetAPIKeyFromContext(r.Context())
	if apiKey == nil {
		h.respondError(w, http.StatusUnauthorized, "Unauthorized")
		return
	}

	h.respondJSON(w, http.StatusOK, map[string]interface{}{
		"user_id":      apiKey.UserID,
		"key_prefix":   apiKey.KeyPrefix,
		"key_name":     apiKey.Name,
		"expires_at":   apiKey.ExpiresAt,
		"last_used_at": apiKey.LastUsedAt,
	})
}

// respondJSON 返回 JSON 响应
func (h *APIKeyHandler) respondJSON(w http.ResponseWriter, statusCode int, data interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(data)
}

// respondError 返回错误响应
func (h *APIKeyHandler) respondError(w http.ResponseWriter, statusCode int, message string) {
	h.respondJSON(w, statusCode, map[string]string{
		"error": message,
	})
}
