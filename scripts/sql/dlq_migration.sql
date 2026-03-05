-- DLQ 死信队列失败记录表
CREATE TABLE IF NOT EXISTS `dlq_failures` (
    `id` BIGINT UNSIGNED PRIMARY KEY AUTO_INCREMENT,
    `topic` VARCHAR(255) NOT NULL COMMENT '原始 topic',
    `partition` INT NOT NULL COMMENT '原始分区',
    `offset` BIGINT NOT NULL COMMENT '原始 offset',

    -- 业务字段
    `user_id` VARCHAR(64) DEFAULT NULL COMMENT '用户 ID',
    `object_key` VARCHAR(512) DEFAULT NULL COMMENT 'OSS 对象键',
    `file_hash` VARCHAR(128) DEFAULT NULL COMMENT '文件哈希',
    `event_id` VARCHAR(64) DEFAULT NULL COMMENT '事件 ID',

    -- 原始消息
    `original_key` VARCHAR(255) DEFAULT NULL COMMENT '原始消息 key',
    `original_value` MEDIUMBLOB COMMENT '原始消息内容',

    -- 失败信息
    `failure_reason` TEXT COMMENT '失败原因',
    `failure_count` INT NOT NULL DEFAULT 1 COMMENT '失败次数',
    `error_category` VARCHAR(32) DEFAULT NULL COMMENT '错误类型: retryable/non_retryable/unknown',
    `error_stack` TEXT COMMENT '错误堆栈',
    `first_failed_at` TIMESTAMP NOT NULL COMMENT '首次失败时间',
    `last_failed_at` TIMESTAMP NOT NULL COMMENT '最后失败时间',

    -- 状态管理
    `status` VARCHAR(16) NOT NULL DEFAULT 'pending' COMMENT '状态: pending/retrying/failed/resolved',
    `resolved_at` TIMESTAMP NULL DEFAULT NULL COMMENT '解决时间',
    `resolved_by` VARCHAR(128) DEFAULT NULL COMMENT '解决人/系统',
    `resolution` TEXT COMMENT '解决方案说明',

    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    -- 索引
    INDEX `idx_topic_offset` (`topic`, `partition`, `offset`),
    INDEX `idx_user_id` (`user_id`),
    INDEX `idx_event_id` (`event_id`),
    INDEX `idx_category` (`error_category`),
    INDEX `idx_first_failed` (`first_failed_at`),
    INDEX `idx_last_failed` (`last_failed_at`),
    INDEX `idx_status_last_failed` (`status`, `last_failed_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='DLQ 死信队列失败记录表';
