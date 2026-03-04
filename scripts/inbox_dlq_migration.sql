-- 更新 Inbox 表，添加 DLQ 支持

-- 1. 添加新的状态值 'DLQ'
ALTER TABLE `inbox` MODIFY COLUMN `status` VARCHAR(16) NOT NULL COMMENT '状态: PROCESSING/DONE/FAILED/DLQ';

-- 2. 添加 DLQ 关联字段
ALTER TABLE `inbox` ADD COLUMN `dlq_failure_id` BIGINT UNSIGNED DEFAULT NULL COMMENT '关联的DLQ失败记录ID' AFTER `last_error`;

-- 3. 添加索引
ALTER TABLE `inbox` ADD INDEX `idx_dlq_failure_id` (`dlq_failure_id`);

-- 4. 添加外键约束（可选，如果需要强制引用完整性）
-- ALTER TABLE `inbox` ADD CONSTRAINT `fk_inbox_dlq_failure`
--   FOREIGN KEY (`dlq_failure_id`) REFERENCES `dlq_failures`(`id`) ON DELETE SET NULL;
