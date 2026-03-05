-- MCP API Key 认证系统数据库迁移脚本

-- 1. 创建 API Key 表
CREATE TABLE IF NOT EXISTS `mcp_api_keys` (
    `id` BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `key_hash` VARCHAR(128) NOT NULL COMMENT 'API Key 哈希值（SHA256）',
    `key_prefix` VARCHAR(16) NOT NULL COMMENT 'Key 前缀（用于显示，如 mcp_xxxxxxxx）',
    `name` VARCHAR(128) DEFAULT NULL COMMENT 'Key 名称/描述',
    `status` VARCHAR(16) NOT NULL DEFAULT 'active' COMMENT '状态: active/revoked/expired',
    `expires_at` TIMESTAMP NULL DEFAULT NULL COMMENT '过期时间',
    `last_used_at` TIMESTAMP NULL DEFAULT NULL COMMENT '最后使用时间',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    -- 唯一索引：一个用户只能有一个 active 状态的 Key
    UNIQUE INDEX `uk_user_id_active` (`user_id`, `status`),
    UNIQUE INDEX `uk_key_hash` (`key_hash`),
    INDEX `idx_key_prefix` (`key_prefix`),
    INDEX `idx_status` (`status`),
    INDEX `idx_expires_at` (`expires_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='MCP API Key 表';

-- 2. 创建 API Key 使用日志表
CREATE TABLE IF NOT EXISTS `mcp_api_key_logs` (
    `id` BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    `api_key_id` BIGINT UNSIGNED NOT NULL COMMENT 'API Key ID',
    `user_id` BIGINT UNSIGNED NOT NULL COMMENT '用户ID',
    `endpoint` VARCHAR(255) NOT NULL COMMENT '请求端点',
    `method` VARCHAR(16) NOT NULL COMMENT 'HTTP 方法',
    `ip_address` VARCHAR(64) NOT NULL COMMENT '请求IP',
    `user_agent` VARCHAR(512) DEFAULT NULL COMMENT 'User Agent',
    `status_code` INT NOT NULL COMMENT '响应状态码',
    `error_message` TEXT COMMENT '错误信息',
    `request_time` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '请求时间',

    INDEX `idx_api_key_id` (`api_key_id`),
    INDEX `idx_user_id` (`user_id`),
    INDEX `idx_request_time` (`request_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='MCP API Key 使用日志';

-- 3. 创建 Rate Limiting 表
CREATE TABLE IF NOT EXISTS `mcp_rate_limits` (
    `id` BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    `api_key_id` BIGINT UNSIGNED NOT NULL COMMENT 'API Key ID',
    `window_start` TIMESTAMP NOT NULL COMMENT '时间窗口开始（分钟级）',
    `request_count` INT NOT NULL DEFAULT 0 COMMENT '请求次数',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE INDEX `uk_key_window` (`api_key_id`, `window_start`),
    INDEX `idx_window_start` (`window_start`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='MCP Rate Limiting';
