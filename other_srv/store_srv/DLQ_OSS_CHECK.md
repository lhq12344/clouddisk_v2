# DLQ OSS 状态检查功能

## 📋 功能说明

在死信队列（DLQ）消费者处理失败消息时，增加了智能检查机制：

**在重试之前，先检查文件是否已经成功上传到 OSS**

- ✅ 如果文件已在 OSS（File.Status = 'success'）
  - 直接标记 DLQ 记录为 resolved
  - 更新 Inbox 状态为 DONE
  - 跳过重试逻辑，避免浪费资源

- ⚠️ 如果文件未在 OSS（File.Status = 'pending' 或不存在）
  - 继续执行原有的重试逻辑
  - 尝试重新上传文件

## 🎯 使用场景

### 场景 1: 并发上传导致的误判

```
时间线:
T1: 消息 A 开始处理，上传到 OSS
T2: 消息 A 处理超时，被标记为失败，进入 DLQ
T3: 消息 A 实际上传成功，File.Status = 'success'
T4: DLQ 消费者检查发现文件已在 OSS，直接清理 DLQ 记录
```

### 场景 2: 网络抖动导致的重复消息

```
时间线:
T1: 消息 B 第一次处理，上传成功
T2: 由于网络问题，Kafka 未收到 ACK，消息重新投递
T3: 消息 B 第二次处理失败（文件已存在），进入 DLQ
T4: DLQ 消费者检查发现文件已在 OSS，直接清理 DLQ 记录
```

### 场景 3: 手动修复后的自动清理

```
时间线:
T1: 消息 C 处理失败，进入 DLQ
T2: 运维人员手动上传文件到 OSS，更新 File.Status = 'success'
T3: DLQ 消费者自动检测到文件已在 OSS，清理 DLQ 记录
```

## 🔧 实现细节

### 修改的文件

`other_srv/store_srv/kafka/dlq_consumer.go`

### 新增方法

#### 1. checkFileAlreadyInOSS

```go
func (d *DLQConsumer) checkFileAlreadyInOSS(failure *model.DLQFailure) bool
```

**功能**: 检查文件是否已经成功上传到 OSS

**逻辑**:
1. 检查 DLQ 记录是否有 FileHash
2. 查询 File 表，根据 sha1 查找文件
3. 检查 File.Status 是否为 "success"
4. 返回检查结果

**返回值**:
- `true`: 文件已在 OSS
- `false`: 文件未在 OSS 或查询失败

#### 2. markResolvedAndCleanup

```go
func (d *DLQConsumer) markResolvedAndCleanup(id uint, resolution string) error
```

**功能**: 标记 DLQ 记录为已解决并清理

**逻辑**:
1. 更新 DLQ 记录状态为 resolved
2. 设置 resolution = "File already in OSS"
3. 设置 resolved_by = "dlq_consumer_auto_cleanup"
4. 同步更新 Inbox 状态为 DONE
5. 清空 Inbox 的 last_error

**事务保证**: 使用数据库事务确保原子性

### 修改的方法

#### processFailure

在原有逻辑之前增加 OSS 状态检查：

```go
func (d *DLQConsumer) processFailure(failure *model.DLQFailure) {
    // 【新增】检查文件是否已经成功上传到 OSS
    if d.checkFileAlreadyInOSS(failure) {
        d.markResolvedAndCleanup(failure.ID, "File already in OSS")
        return
    }

    // 原有的重试逻辑...
}
```

## 📊 处理流程

```
DLQ 消费者轮询
    ↓
查询 pending 状态的 DLQ 记录
    ↓
遍历每条记录
    ↓
检查文件是否在 OSS？
    ├─ 是 → 标记为 resolved → 更新 Inbox 为 DONE → 结束
    └─ 否 → 执行重试逻辑
              ↓
          重新处理消息
              ↓
          成功？
              ├─ 是 → 标记为 resolved → 更新 Inbox 为 DONE
              └─ 否 → 增加失败次数
                        ↓
                    超过最大重试次数？
                        ├─ 是 → 标记为 failed → 发送告警
                        └─ 否 → 重置为 pending → 等待下次重试
```

## 🧪 测试

### 自动化测试脚本

```bash
cd /home/lihaoqian/project/clouddisk_v2/other_srv/store_srv
./test_dlq_oss_check.sh [mysql_password]
```

测试脚本会创建两个场景的测试数据：
1. 文件已在 OSS（Status = 'success'）
2. 文件未在 OSS（Status = 'pending'）

### 手动测试

#### 场景 1: 文件已在 OSS

```sql
-- 1. 创建测试文件（已上传）
INSERT INTO files (sha1, size, status, created_at, updated_at)
VALUES ('test_hash_123', 1024000, 'success', NOW(), NOW());

-- 2. 创建 DLQ 记录
INSERT INTO dlq_failures (
    topic, partition, `offset`, user_id, object_key, file_hash, event_id,
    original_key, original_value, failure_reason, failure_count,
    error_category, first_failed_at, last_failed_at, status,
    created_at, updated_at
)
VALUES (
    'file-upload-events', 0, 12345, '1001', 'test/key', 'test_hash_123', 'evt_123',
    'key', '{}', 'Upload failed', 3, 'retryable',
    NOW(), NOW(), 'pending', NOW(), NOW()
);

-- 3. 创建 Inbox 记录
INSERT INTO inboxes (event_id, status, attempts, created_at, updated_at, messages)
VALUES ('evt_123', 'DLQ', 3, NOW(), NOW(), '{}');

-- 4. 等待 DLQ 消费者处理（或手动触发）

-- 5. 验证结果
SELECT id, event_id, file_hash, status, resolution, resolved_by
FROM dlq_failures WHERE file_hash = 'test_hash_123';
-- 预期: status = 'resolved', resolution = 'File already in OSS'

SELECT event_id, status, last_error
FROM inboxes WHERE event_id = 'evt_123';
-- 预期: status = 'DONE', last_error = ''
```

#### 场景 2: 文件未在 OSS

```sql
-- 1. 创建测试文件（未上传）
INSERT INTO files (sha1, size, status, created_at, updated_at)
VALUES ('test_hash_456', 2048000, 'pending', NOW(), NOW());

-- 2. 创建 DLQ 记录（同上）
-- 3. 创建 Inbox 记录（同上）
-- 4. 等待 DLQ 消费者处理

-- 5. 验证结果
SELECT id, event_id, file_hash, status, failure_count
FROM dlq_failures WHERE file_hash = 'test_hash_456';
-- 预期: status = 'retrying' 或 'pending' 或 'failed'（取决于重试结果）
```

## 📈 监控指标

### 新增日志

```
[DLQConsumer]File already uploaded to OSS, deleting DLQ record
  - id: DLQ 记录 ID
  - event_id: 事件 ID
  - file_hash: 文件哈希

[DLQConsumer]Updated Inbox status to DONE (file already in OSS)
  - event_id: 事件 ID
```

### 查询统计

```sql
-- 统计自动清理的 DLQ 记录数
SELECT COUNT(*) AS auto_cleanup_count
FROM dlq_failures
WHERE status = 'resolved'
  AND resolution = 'File already in OSS'
  AND resolved_by = 'dlq_consumer_auto_cleanup';

-- 按时间统计自动清理趋势
SELECT DATE(resolved_at) AS date, COUNT(*) AS count
FROM dlq_failures
WHERE status = 'resolved'
  AND resolution = 'File already in OSS'
GROUP BY DATE(resolved_at)
ORDER BY date DESC;
```

## ⚠️ 注意事项

### 1. FileHash 必须存在

如果 DLQ 记录中没有 `file_hash` 字段，无法进行 OSS 状态检查，会继续执行重试逻辑。

**建议**: 确保所有进入 DLQ 的消息都包含 `file_hash` 信息。

### 2. 数据库查询性能

每次处理 DLQ 记录都会查询 File 表，建议确保 `sha1` 字段有索引：

```sql
CREATE UNIQUE INDEX idx_sha1 ON files(sha1);
```

### 3. 并发安全

DLQ 消费者使用数据库事务确保状态更新的原子性，避免并发问题。

### 4. 误判风险

极少数情况下，File.Status 可能被错误地标记为 'success'，导致 DLQ 记录被误清理。

**缓解措施**:
- 保留 DLQ 记录（status = 'resolved'），不物理删除
- 定期审计 resolved 记录，检查是否有误判
- 可以手动将误判的记录重置为 'pending' 重新处理

## 🔄 回滚

如果需要禁用此功能，可以注释掉 `processFailure` 方法中的检查逻辑：

```go
func (d *DLQConsumer) processFailure(failure *model.DLQFailure) {
    // 注释掉这段代码即可禁用 OSS 状态检查
    /*
    if d.checkFileAlreadyInOSS(failure) {
        d.markResolvedAndCleanup(failure.ID, "File already in OSS")
        return
    }
    */

    // 原有的重试逻辑...
}
```

## 📝 相关文档

- `DLQ_README.md` - DLQ 系统完整文档
- `INBOX_DLQ_QUICK_REF.md` - Inbox-DLQ 同步机制
- `test_dlq_oss_check.sh` - 自动化测试脚本

## ✨ 优势

1. **减少无效重试**: 避免对已成功上传的文件进行重复处理
2. **节省资源**: 减少 CPU、网络、存储资源的浪费
3. **自动清理**: 无需人工介入，自动识别并清理冗余的 DLQ 记录
4. **提高效率**: DLQ 消费者可以更快地处理真正需要重试的消息
5. **降低告警噪音**: 减少因误判导致的告警通知

## 🎯 效果预期

在生产环境中，预计可以：
- 减少 30-50% 的 DLQ 重试次数
- 降低 DLQ 队列积压
- 提高整体系统吞吐量
