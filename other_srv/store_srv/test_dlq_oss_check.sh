#!/bin/bash

# DLQ OSS 状态检查功能测试脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

MYSQL_PASSWORD="${1:-123456}"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}DLQ OSS 状态检查功能测试${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 检查数据库连接
echo -e "${YELLOW}检查数据库连接...${NC}"
if ! mysql -u root -p"$MYSQL_PASSWORD" -e "SELECT 1" &>/dev/null; then
    echo -e "${RED}✗ 数据库连接失败！${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 数据库连接成功${NC}"
echo ""

# 测试场景 1: 文件已在 OSS，DLQ 应该被清理
echo -e "${YELLOW}测试场景 1: 文件已在 OSS，DLQ 应该被清理${NC}"
echo ""

# 1.1 创建一个测试文件记录（状态为 success）
TEST_HASH="test_hash_$(date +%s)"
echo "创建测试文件记录: $TEST_HASH"

mysql -u root -p"$MYSQL_PASSWORD" clouddisk <<EOF
-- 插入测试文件（状态为 success，表示已上传到 OSS）
INSERT INTO files (sha1, size, status, created_at, updated_at)
VALUES ('$TEST_HASH', 1024000, 'success', NOW(), NOW());

-- 获取文件 ID
SET @file_id = LAST_INSERT_ID();

-- 插入 Inbox 记录（状态为 DLQ）
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, last_error, dlq_failure_id, created_at, updated_at, messages)
VALUES ('test_event_oss_success', 'DLQ', '', NULL, 3, 'Upload failed', NULL, NOW(), NOW(), '{}');

-- 插入 DLQ 记录（状态为 pending）
INSERT INTO dlq_failures (
    topic, partition, \`offset\`, user_id, object_key, file_hash, event_id,
    original_key, original_value, failure_reason, failure_count,
    error_category, error_stack, first_failed_at, last_failed_at,
    status, resolved_at, resolved_by, resolution, created_at, updated_at
)
VALUES (
    'file-upload-events', 0, 12345, '1001', 'test/object/key', '$TEST_HASH', 'test_event_oss_success',
    'test_key', '{"test":"data"}', 'OSS upload failed', 3,
    'retryable', 'stack trace here', NOW(), NOW(),
    'pending', NULL, NULL, NULL, NOW(), NOW()
);

SELECT '✓ 测试数据插入成功' AS result;
EOF

echo -e "${GREEN}✓ 测试数据创建完成${NC}"
echo ""

# 1.2 查询 DLQ 记录
echo "查询 DLQ 记录（处理前）:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT id, event_id, file_hash, status, failure_count
FROM dlq_failures
WHERE file_hash = '$TEST_HASH';
"
echo ""

# 1.3 查询 Inbox 记录
echo "查询 Inbox 记录（处理前）:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT event_id, status, attempts, last_error
FROM inboxes
WHERE event_id = 'test_event_oss_success';
"
echo ""

echo -e "${BLUE}提示: 现在需要手动触发 DLQ 消费者处理${NC}"
echo -e "${BLUE}DLQ 消费者会检测到文件已在 OSS，自动清理此记录${NC}"
echo ""
echo "预期结果:"
echo "  1. DLQ 记录状态变为 'resolved'"
echo "  2. DLQ 记录的 resolution 为 'File already in OSS'"
echo "  3. Inbox 记录状态变为 'DONE'"
echo ""

# 测试场景 2: 文件未在 OSS，DLQ 应该继续重试
echo -e "${YELLOW}测试场景 2: 文件未在 OSS，DLQ 应该继续重试${NC}"
echo ""

# 2.1 创建一个测试文件记录（状态为 pending）
TEST_HASH2="test_hash_pending_$(date +%s)"
echo "创建测试文件记录: $TEST_HASH2"

mysql -u root -p"$MYSQL_PASSWORD" clouddisk <<EOF
-- 插入测试文件（状态为 pending，表示未上传到 OSS）
INSERT INTO files (sha1, size, status, created_at, updated_at)
VALUES ('$TEST_HASH2', 2048000, 'pending', NOW(), NOW());

-- 插入 Inbox 记录（状态为 DLQ）
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, last_error, dlq_failure_id, created_at, updated_at, messages)
VALUES ('test_event_oss_pending', 'DLQ', '', NULL, 3, 'Upload failed', NULL, NOW(), NOW(), '{}');

-- 插入 DLQ 记录（状态为 pending）
INSERT INTO dlq_failures (
    topic, partition, \`offset\`, user_id, object_key, file_hash, event_id,
    original_key, original_value, failure_reason, failure_count,
    error_category, error_stack, first_failed_at, last_failed_at,
    status, resolved_at, resolved_by, resolution, created_at, updated_at
)
VALUES (
    'file-upload-events', 0, 12346, '1002', 'test/object/key2', '$TEST_HASH2', 'test_event_oss_pending',
    'test_key2', '{"test":"data2"}', 'OSS upload failed', 2,
    'retryable', 'stack trace here', NOW(), NOW(),
    'pending', NULL, NULL, NULL, NOW(), NOW()
);

SELECT '✓ 测试数据插入成功' AS result;
EOF

echo -e "${GREEN}✓ 测试数据创建完成${NC}"
echo ""

# 2.2 查询 DLQ 记录
echo "查询 DLQ 记录（处理前）:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT id, event_id, file_hash, status, failure_count
FROM dlq_failures
WHERE file_hash = '$TEST_HASH2';
"
echo ""

echo -e "${BLUE}提示: 现在需要手动触发 DLQ 消费者处理${NC}"
echo -e "${BLUE}DLQ 消费者会检测到文件未在 OSS，继续执行重试逻辑${NC}"
echo ""
echo "预期结果:"
echo "  1. DLQ 记录状态先变为 'retrying'"
echo "  2. 如果重试失败，状态变回 'pending' 或 'failed'"
echo "  3. Inbox 记录状态保持 'DLQ' 或变为 'DONE'（如果重试成功）"
echo ""

# 验证查询
echo -e "${YELLOW}验证查询命令（DLQ 消费者处理后执行）:${NC}"
echo ""
echo "# 场景 1: 检查文件已在 OSS 的 DLQ 记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT id, event_id, file_hash, status, resolution, resolved_by"
echo "  FROM dlq_failures"
echo "  WHERE file_hash = '$TEST_HASH';\\"
echo ""
echo "# 场景 1: 检查对应的 Inbox 记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT event_id, status, last_error"
echo "  FROM inboxes"
echo "  WHERE event_id = 'test_event_oss_success';\\"
echo ""
echo "# 场景 2: 检查文件未在 OSS 的 DLQ 记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT id, event_id, file_hash, status, failure_count"
echo "  FROM dlq_failures"
echo "  WHERE file_hash = '$TEST_HASH2';\\"
echo ""

# 清理命令
echo -e "${YELLOW}清理测试数据命令:${NC}"
echo ""
echo "mysql -u root -p clouddisk <<EOF"
echo "DELETE FROM dlq_failures WHERE file_hash IN ('$TEST_HASH', '$TEST_HASH2');"
echo "DELETE FROM inboxes WHERE event_id IN ('test_event_oss_success', 'test_event_oss_pending');"
echo "DELETE FROM files WHERE sha1 IN ('$TEST_HASH', '$TEST_HASH2');"
echo "EOF"
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}测试数据准备完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "下一步操作:"
echo "1. 启动或重启 store_srv（包含 DLQ 消费者）"
echo "2. 等待 DLQ 消费者轮询（默认 5 分钟间隔）"
echo "3. 或者手动触发 DLQ 处理逻辑"
echo "4. 使用上面的验证查询命令检查结果"
echo "5. 使用清理命令删除测试数据"
echo ""
