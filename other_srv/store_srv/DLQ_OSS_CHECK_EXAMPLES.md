# DLQ OSS 状态检查 - 使用示例

## 快速开始

### 1. 功能已自动启用

DLQ OSS 状态检查功能已集成到 DLQ 消费者中，无需额外配置。

当 `store_srv` 启动时，DLQ 消费者会自动：
1. 每 5 分钟轮询一次 pending 状态的 DLQ 记录
2. 对每条记录先检查文件是否在 OSS
3. 根据检查结果决定是清理还是重试

### 2. 验证功能是否生效

查看日志中是否有以下关键信息：

```bash
# 启动 store_srv
cd /home/lihaoqian/project/clouddisk_v2/other_srv/store_srv
go run .

# 查看日志（在另一个终端）
tail -f /path/to/store_srv.log | grep -E "DLQConsumer|OSS"
```

预期日志输出：
```
[DLQConsumer]Starting DLQ consumer poll_interval=5m0s batch_size=10
[DLQConsumer]Processing DLQ batch
[DLQConsumer]Found pending DLQ messages count=2
[DLQConsumer]Processing DLQ failure id=1 event_id=evt_123 file_hash=abc123
[DLQConsumer]File already uploaded to OSS file_hash=abc123 file_id=456 size=1024000
[DLQConsumer]File already uploaded to OSS, deleting DLQ record id=1 event_id=evt_123
[DLQConsumer]Updated Inbox status to DONE (file already in OSS) event_id=evt_123
[DLQConsumer]DLQ record cleaned up id=1 event_id=evt_123 file_hash=abc123
```

## 实际场景示例

### 场景 1: 并发上传导致的 DLQ 误判

**问题描述**:
- 用户上传大文件，处理时间较长
- Kafka 消费者超时，消息被标记为失败
- 实际上文件已经成功上传到 OSS
- 消息进入 DLQ

**自动处理**:
```
1. DLQ 消费者轮询到这条记录
2. 检查 File 表: sha1='abc123', status='success' ✓
3. 判断: 文件已在 OSS
4. 自动清理:
   - DLQ 记录 → status='resolved', resolution='File already in OSS'
   - Inbox 记录 → status='DONE'
5. 完成，无需人工介入
```

**数据库验证**:
```sql
-- 查看 DLQ 记录
SELECT id, event_id, file_hash, status, resolution, resolved_by
FROM dlq_failures
WHERE event_id = 'evt_123';

-- 结果:
-- id | event_id | file_hash | status   | resolution            | resolved_by
-- 1  | evt_123  | abc123    | resolved | File already in OSS   | dlq_consumer_auto_cleanup

-- 查看 Inbox 记录
SELECT event_id, status, last_error
FROM inboxes
WHERE event_id = 'evt_123';

-- 结果:
-- event_id | status | last_error
-- evt_123  | DONE   | (empty)
```

### 场景 2: 网络抖动导致的重复消息

**问题描述**:
- 消息 A 第一次处理成功，文件上传到 OSS
- 网络抖动，Kafka 未收到 ACK
- 消息 A 被重新投递
- 第二次处理时发现文件已存在，报错进入 DLQ

**自动处理**:
```
1. DLQ 消费者检测到文件已在 OSS
2. 自动清理 DLQ 记录
3. 更新 Inbox 为 DONE
4. 避免重复上传
```

### 场景 3: 手动修复后的自动清理

**问题描述**:
- 消息处理失败，进入 DLQ
- 运维人员手动上传文件到 OSS
- 手动更新数据库: `UPDATE files SET status='success' WHERE sha1='xyz789'`

**自动处理**:
```
1. 下次 DLQ 消费者轮询时
2. 检测到文件已在 OSS
3. 自动清理 DLQ 记录
4. 无需手动删除 DLQ 记录
```

**手动修复步骤**:
```sql
-- 1. 手动上传文件到 OSS（使用 MinIO 客户端或 OSS 控制台）

-- 2. 更新数据库
UPDATE files
SET status = 'success', updated_at = NOW()
WHERE sha1 = 'xyz789';

-- 3. 等待 DLQ 消费者自动清理（最多 5 分钟）
-- 或者手动触发 DLQ 处理

-- 4. 验证清理结果
SELECT id, event_id, status, resolution
FROM dlq_failures
WHERE file_hash = 'xyz789';
```

## 监控和统计

### 查看自动清理统计

```sql
-- 今天自动清理的 DLQ 记录数
SELECT COUNT(*) AS auto_cleanup_today
FROM dlq_failures
WHERE status = 'resolved'
  AND resolution = 'File already in OSS'
  AND DATE(resolved_at) = CURDATE();

-- 最近 7 天的自动清理趋势
SELECT
    DATE(resolved_at) AS date,
    COUNT(*) AS cleanup_count
FROM dlq_failures
WHERE status = 'resolved'
  AND resolution = 'File already in OSS'
  AND resolved_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)
GROUP BY DATE(resolved_at)
ORDER BY date DESC;

-- 自动清理率（清理数 / 总 DLQ 数）
SELECT
    SUM(CASE WHEN resolution = 'File already in OSS' THEN 1 ELSE 0 END) AS auto_cleanup,
    COUNT(*) AS total_resolved,
    ROUND(SUM(CASE WHEN resolution = 'File already in OSS' THEN 1 ELSE 0 END) * 100.0 / COUNT(*), 2) AS cleanup_rate_percent
FROM dlq_failures
WHERE status = 'resolved'
  AND resolved_at >= DATE_SUB(NOW(), INTERVAL 7 DAY);
```

### 查看需要人工介入的 DLQ 记录

```sql
-- 查看最终失败的 DLQ 记录（需要人工处理）
SELECT
    id,
    event_id,
    file_hash,
    user_id,
    failure_reason,
    failure_count,
    first_failed_at,
    last_failed_at
FROM dlq_failures
WHERE status = 'failed'
ORDER BY last_failed_at DESC
LIMIT 20;

-- 查看长时间未解决的 DLQ 记录
SELECT
    id,
    event_id,
    file_hash,
    user_id,
    failure_count,
    TIMESTAMPDIFF(HOUR, first_failed_at, NOW()) AS hours_in_dlq
FROM dlq_failures
WHERE status IN ('pending', 'retrying')
  AND first_failed_at < DATE_SUB(NOW(), INTERVAL 24 HOUR)
ORDER BY first_failed_at ASC;
```

## 性能影响

### 额外的数据库查询

每处理一条 DLQ 记录，会增加一次 File 表查询：

```sql
SELECT * FROM files WHERE sha1 = ? LIMIT 1;
```

**性能优化**:
- 确保 `sha1` 字段有唯一索引
- 查询速度通常 < 1ms
- 对整体性能影响可忽略不计

### 资源节省

通过避免无效重试，可以节省：
- CPU: 减少文件上传处理
- 网络: 减少 OSS 请求
- 存储: 减少临时文件
- 时间: 加快 DLQ 队列处理

**预期效果**:
- 减少 30-50% 的 DLQ 重试次数
- 降低 DLQ 队列积压
- 提高整体系统吞吐量

## 故障排查

### 问题 1: DLQ 记录未被自动清理

**可能原因**:
1. DLQ 消费者未启动
2. File 表中没有对应记录
3. File.Status 不是 'success'
4. DLQ 记录的 file_hash 为空

**排查步骤**:
```sql
-- 1. 检查 DLQ 记录
SELECT id, event_id, file_hash, status, failure_count
FROM dlq_failures
WHERE id = 123;

-- 2. 检查 File 记录
SELECT id, sha1, size, status
FROM files
WHERE sha1 = 'abc123';

-- 3. 检查日志
tail -f /path/to/store_srv.log | grep "DLQConsumer"
```

### 问题 2: 文件实际未上传，但被误清理

**极少数情况**: File.Status 被错误标记为 'success'

**恢复步骤**:
```sql
-- 1. 查找被误清理的记录
SELECT id, event_id, file_hash, resolved_at
FROM dlq_failures
WHERE status = 'resolved'
  AND resolution = 'File already in OSS'
  AND resolved_at > DATE_SUB(NOW(), INTERVAL 1 HOUR);

-- 2. 验证文件是否真的在 OSS
-- （使用 MinIO 客户端或 OSS 控制台检查）

-- 3. 如果文件确实不在 OSS，重置 DLQ 记录
UPDATE dlq_failures
SET
    status = 'pending',
    resolved_at = NULL,
    resolved_by = NULL,
    resolution = NULL,
    failure_count = 0
WHERE id = 123;

-- 4. 更新 Inbox 状态
UPDATE inboxes
SET status = 'DLQ'
WHERE event_id = 'evt_123';
```

## 最佳实践

### 1. 定期审计

每周审计一次自动清理的记录，确保没有误判：

```sql
-- 抽查最近 7 天自动清理的记录
SELECT
    d.id,
    d.event_id,
    d.file_hash,
    d.resolved_at,
    f.id AS file_id,
    f.status AS file_status,
    f.size AS file_size
FROM dlq_failures d
LEFT JOIN files f ON d.file_hash = f.sha1
WHERE d.status = 'resolved'
  AND d.resolution = 'File already in OSS'
  AND d.resolved_at >= DATE_SUB(NOW(), INTERVAL 7 DAY)
ORDER BY d.resolved_at DESC
LIMIT 50;
```

### 2. 监控告警

设置告警规则：
- DLQ 自动清理率突然下降（< 20%）
- DLQ 队列积压超过阈值（> 100 条）
- 最终失败的 DLQ 记录增加

### 3. 保留历史记录

不要物理删除 resolved 状态的 DLQ 记录，保留用于：
- 问题追溯
- 数据分析
- 审计合规

定期归档（如 90 天后）：
```sql
-- 归档 90 天前的 resolved 记录到历史表
INSERT INTO dlq_failures_archive
SELECT * FROM dlq_failures
WHERE status = 'resolved'
  AND resolved_at < DATE_SUB(NOW(), INTERVAL 90 DAY);

-- 删除已归档的记录
DELETE FROM dlq_failures
WHERE status = 'resolved'
  AND resolved_at < DATE_SUB(NOW(), INTERVAL 90 DAY);
```

## 相关文档

- `DLQ_OSS_CHECK.md` - 功能详细说明
- `DLQ_README.md` - DLQ 系统完整文档
- `INBOX_DLQ_QUICK_REF.md` - Inbox-DLQ 同步机制
- `test_dlq_oss_check.sh` - 自动化测试脚本
