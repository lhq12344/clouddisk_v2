#!/bin/bash

# Inbox 锁定机制修复验证脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

MYSQL_PASSWORD="${1:-123456}"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}Inbox 锁定机制修复验证${NC}"
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

# 测试场景 1: 模拟 Inbox 被锁定的情况
echo -e "${YELLOW}测试场景 1: 模拟 Inbox 被锁定${NC}"
echo ""

TEST_EVENT_ID="test_lock_$(date +%s)"
LOCK_UNTIL=$(date -d "+2 minutes" "+%Y-%m-%d %H:%M:%S")

echo "创建被锁定的 Inbox 记录: $TEST_EVENT_ID"
echo "锁定到: $LOCK_UNTIL"

mysql -u root -p"$MYSQL_PASSWORD" clouddisk <<EOF
-- 插入被锁定的 Inbox 记录
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, last_error, created_at, updated_at, messages)
VALUES ('$TEST_EVENT_ID', 'PROCESSING', 'worker-1', '$LOCK_UNTIL', 1, '', NOW(), NOW(), '{}');

SELECT '✓ 测试数据插入成功' AS result;
EOF

echo -e "${GREEN}✓ 测试数据创建完成${NC}"
echo ""

# 查询 Inbox 记录
echo "查询 Inbox 记录:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT event_id, status, locked_by, locked_until, attempts
FROM inboxes
WHERE event_id = '$TEST_EVENT_ID';
"
echo ""

echo -e "${BLUE}说明:${NC}"
echo "1. 此 Inbox 记录被 worker-1 锁定，锁定到 $LOCK_UNTIL"
echo "2. 如果其他 worker 尝试处理此消息，会返回 ErrInboxLocked"
echo "3. 不会提交 offset，Kafka 会重新投递消息"
echo "4. 等待锁过期后，其他 worker 可以接管处理"
echo ""

# 测试场景 2: 验证锁过期后的接管
echo -e "${YELLOW}测试场景 2: 验证锁过期后的接管${NC}"
echo ""

TEST_EVENT_ID2="test_expired_$(date +%s)"
LOCK_UNTIL_EXPIRED=$(date -d "-1 minutes" "+%Y-%m-%d %H:%M:%S")

echo "创建锁已过期的 Inbox 记录: $TEST_EVENT_ID2"
echo "锁定到: $LOCK_UNTIL_EXPIRED (已过期)"

mysql -u root -p"$MYSQL_PASSWORD" clouddisk <<EOF
-- 插入锁已过期的 Inbox 记录
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, last_error, created_at, updated_at, messages)
VALUES ('$TEST_EVENT_ID2', 'PROCESSING', 'worker-1', '$LOCK_UNTIL_EXPIRED', 1, '', NOW(), NOW(), '{}');

SELECT '✓ 测试数据插入成功' AS result;
EOF

echo -e "${GREEN}✓ 测试数据创建完成${NC}"
echo ""

# 查询 Inbox 记录
echo "查询 Inbox 记录:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT event_id, status, locked_by, locked_until, attempts,
       CASE
           WHEN locked_until < NOW() THEN '已过期'
           ELSE '未过期'
       END AS lock_status
FROM inboxes
WHERE event_id = '$TEST_EVENT_ID2';
"
echo ""

echo -e "${BLUE}说明:${NC}"
echo "1. 此 Inbox 记录的锁已过期"
echo "2. 其他 worker 可以接管处理（通过 tryBeginInbox 更新锁）"
echo "3. 接管后，locked_by 会更新为新 worker 的 ID"
echo "4. attempts 会增加 1"
echo ""

# 测试场景 3: 验证 DONE 状态的幂等性
echo -e "${YELLOW}测试场景 3: 验证 DONE 状态的幂等性${NC}"
echo ""

TEST_EVENT_ID3="test_done_$(date +%s)"

echo "创建已完成的 Inbox 记录: $TEST_EVENT_ID3"

mysql -u root -p"$MYSQL_PASSWORD" clouddisk <<EOF
-- 插入已完成的 Inbox 记录
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, last_error, created_at, updated_at, messages)
VALUES ('$TEST_EVENT_ID3', 'DONE', '', NULL, 1, '', NOW(), NOW(), '{}');

SELECT '✓ 测试数据插入成功' AS result;
EOF

echo -e "${GREEN}✓ 测试数据创建完成${NC}"
echo ""

# 查询 Inbox 记录
echo "查询 Inbox 记录:"
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT event_id, status, locked_by, locked_until, attempts
FROM inboxes
WHERE event_id = '$TEST_EVENT_ID3';
"
echo ""

echo -e "${BLUE}说明:${NC}"
echo "1. 此 Inbox 记录状态为 DONE，表示已处理完成"
echo "2. 如果消息重新投递，tryBeginInbox 会返回 done=true"
echo "3. 消息会被跳过，不会重复处理"
echo "4. offset 会被提交"
echo ""

# 验证查询
echo -e "${YELLOW}验证查询:${NC}"
echo ""

echo "# 查询所有测试记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT event_id, status, locked_by, locked_until, attempts,"
echo "         CASE WHEN locked_until < NOW() THEN '已过期' ELSE '未过期' END AS lock_status"
echo "  FROM inboxes"
echo "  WHERE event_id LIKE 'test_%'"
echo "  ORDER BY created_at DESC;\\"
echo ""

# 实际执行查询
mysql -u root -p"$MYSQL_PASSWORD" clouddisk -e "
SELECT event_id, status, locked_by, locked_until, attempts,
       CASE WHEN locked_until < NOW() THEN '已过期' WHEN locked_until IS NULL THEN 'N/A' ELSE '未过期' END AS lock_status
FROM inboxes
WHERE event_id LIKE 'test_%'
ORDER BY created_at DESC;
"
echo ""

# 监控查询
echo -e "${YELLOW}监控查询（生产环境使用）:${NC}"
echo ""

echo "# 统计当前被锁定的 Inbox 记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT COUNT(*) AS locked_count"
echo "  FROM inboxes"
echo "  WHERE status = 'PROCESSING' AND locked_until > NOW();\\"
echo ""

echo "# 统计锁超时的 Inbox 记录"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT COUNT(*) AS expired_lock_count"
echo "  FROM inboxes"
echo "  WHERE status = 'PROCESSING' AND locked_until < NOW();\\"
echo ""

echo "# 查看长时间处理的消息"
echo "mysql -u root -p clouddisk -e \\"
echo "  SELECT event_id, locked_by, locked_until, attempts,"
echo "         TIMESTAMPDIFF(MINUTE, updated_at, NOW()) AS processing_minutes"
echo "  FROM inboxes"
echo "  WHERE status = 'PROCESSING'"
echo "    AND updated_at < DATE_SUB(NOW(), INTERVAL 5 MINUTE)"
echo "  ORDER BY updated_at ASC;\\"
echo ""

# 清理命令
echo -e "${YELLOW}清理测试数据命令:${NC}"
echo ""
echo "mysql -u root -p clouddisk <<EOF"
echo "DELETE FROM inboxes WHERE event_id IN ('$TEST_EVENT_ID', '$TEST_EVENT_ID2', '$TEST_EVENT_ID3');"
echo "EOF"
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}测试数据准备完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

echo "下一步操作:"
echo ""
echo "1. 启动 store_srv 消费者"
echo "   cd /home/lihaoqian/project/clouddisk_v2/other_srv/store_srv"
echo "   go run ."
echo ""
echo "2. 观察日志，查找以下关键信息:"
echo "   tail -f /path/to/store_srv.log | grep -E 'inbox locked|ErrInboxLocked'"
echo ""
echo "3. 预期日志输出:"
echo "   [NewFileUploadConsumer]inbox locked, will not commit offset"
echo "   [processMessageWithRetry]Inbox locked, skipping retry"
echo ""
echo "4. 验证行为:"
echo "   - 锁定的消息不会提交 offset"
echo "   - Kafka 会重新投递消息"
echo "   - 锁过期后，消息会被成功处理"
echo ""
echo "5. 使用上面的监控查询检查系统状态"
echo ""
echo "6. 完成后使用清理命令删除测试数据"
echo ""
