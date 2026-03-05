# Inbox 锁定机制修复 - 防止消息丢失

## 🐛 问题描述

### 原有问题

在 Kafka 消费者处理消息时，如果 Inbox 记录被其他 worker 锁定（`locked_until` 还没过期），原有逻辑会：

1. `tryBeginInbox` 返回 `acquired=false`
2. `processMessage` 返回错误: `"inbox locked by other worker, retry later"`
3. 进入重试逻辑，重试 3 次
4. 重试耗尽后，**仍然提交 offset**
5. **消息丢失** - 消息未被处理，但 offset 已提交

### 问题流程

```
消息 A 到达 → Worker 1 获取锁（locked_until = T+2分钟）
    ↓
消息 A 重新投递（Kafka 超时）→ Worker 2 尝试处理
    ↓
Worker 2: tryBeginInbox → acquired=false（锁未过期）
    ↓
Worker 2: 返回错误 "inbox locked by other worker"
    ↓
Worker 2: 重试 3 次，仍然失败
    ↓
Worker 2: 提交 offset ❌ 错误！
    ↓
消息丢失 - Worker 1 可能还在处理，但消息已被标记为已消费
```

## ✅ 修复方案

### 核心思路

**当 Inbox 被锁定时，不提交 offset，让 Kafka 重新投递消息**

### 实现细节

#### 1. 定义特殊错误类型

```go
// ErrInboxLocked 表示 Inbox 被其他 worker 锁定，不应提交 offset
var ErrInboxLocked = errors.New("inbox locked by other worker")
```

#### 2. 修改 processMessage

当 Inbox 未获取到锁时，返回 `ErrInboxLocked`：

```go
func (c *FileUploadConsumer) processMessage(ctx context.Context, msg *sarama.ConsumerMessage) error {
    // ...

    acquired, done, err := c.tryBeginInbox(ctx, p.EventID)
    if err != nil {
        return err
    }
    if done {
        return nil // 已处理过，允许 MarkMessage
    }
    if !acquired {
        // 【修复】锁未过期，返回特殊错误，不提交 offset
        return ErrInboxLocked
    }

    // ...
}
```

#### 3. 修改 processMessageWithRetry

遇到 `ErrInboxLocked` 时，直接返回，不进行重试：

```go
func (c *FileUploadConsumer) processMessageWithRetry(ctx context.Context, msg *sarama.ConsumerMessage) error {
    for retryCount <= c.retryConfig.MaxRetries {
        err := c.processMessage(ctx, msg)
        if err == nil {
            return nil
        }

        // 【修复】如果是 Inbox 锁定错误，直接返回，不重试
        if errors.Is(err, ErrInboxLocked) {
            return err
        }

        // 其他错误继续重试逻辑...
    }
}
```

#### 4. 修改 Worker 处理逻辑

遇到 `ErrInboxLocked` 时，不调用 `MarkMessage`：

```go
for t := range c.taskCh {
    err := c.processMessageWithRetry(ctx, t.msg)
    if err == nil {
        // 成功：提交 offset
        t.session.MarkMessage(t.msg, "")
    } else if errors.Is(err, ErrInboxLocked) {
        // 【修复】Inbox 被锁定：不提交 offset，让 Kafka 重新投递
        // 不调用 MarkMessage
    } else {
        // 其他错误：发送到 DLQ 并提交 offset
        t.session.MarkMessage(t.msg, "")
    }
}
```

## 🔄 修复后的流程

```
消息 A 到达 → Worker 1 获取锁（locked_until = T+2分钟）
    ↓
消息 A 重新投递（Kafka 超时）→ Worker 2 尝试处理
    ↓
Worker 2: tryBeginInbox → acquired=false（锁未过期）
    ↓
Worker 2: 返回 ErrInboxLocked
    ↓
Worker 2: 不提交 offset ✓ 正确！
    ↓
Kafka 重新投递消息 A
    ↓
情况 1: Worker 1 完成处理 → Inbox.Status = DONE → Worker 2 跳过
情况 2: 锁过期 → Worker 2 获取锁 → 继续处理
```

## 📊 对比分析

### 修复前

| 场景 | 行为 | 结果 |
|------|------|------|
| Inbox 被锁定 | 重试 3 次 → 提交 offset | ❌ 消息丢失 |
| 处理成功 | 提交 offset | ✓ 正常 |
| 处理失败（可重试） | 重试 3 次 → DLQ → 提交 offset | ✓ 正常 |

### 修复后

| 场景 | 行为 | 结果 |
|------|------|------|
| Inbox 被锁定 | 不提交 offset → Kafka 重新投递 | ✓ 消息不丢失 |
| 处理成功 | 提交 offset | ✓ 正常 |
| 处理失败（可重试） | 重试 3 次 → DLQ → 提交 offset | ✓ 正常 |

## 🧪 测试验证

### 测试场景 1: 并发处理同一消息

```sql
-- 1. 创建测试数据
INSERT INTO inboxes (event_id, status, locked_by, locked_until, attempts, created_at, updated_at, messages)
VALUES ('test_evt_123', 'PROCESSING', 'worker-1', DATE_ADD(NOW(), INTERVAL 2 MINUTE), 1, NOW(), NOW(), '{}');

-- 2. 模拟 Worker 2 尝试处理同一消息
-- Worker 2 会返回 ErrInboxLocked，不提交 offset

-- 3. 验证 Inbox 状态未被 Worker 2 修改
SELECT event_id, status, locked_by, locked_until
FROM inboxes
WHERE event_id = 'test_evt_123';
-- 预期: locked_by 仍然是 'worker-1'

-- 4. 等待锁过期（2 分钟后）
-- Worker 2 重新处理，成功获取锁

-- 5. 验证最终状态
SELECT event_id, status, locked_by, attempts
FROM inboxes
WHERE event_id = 'test_evt_123';
-- 预期: status = 'DONE', locked_by = '', attempts = 2
```

### 测试场景 2: 锁过期后的接管

```go
// 模拟测试代码
func TestInboxLockExpiry(t *testing.T) {
    // 1. Worker 1 获取锁
    acquired1, _, _ := consumer1.tryBeginInbox(ctx, "evt_456")
    assert.True(t, acquired1)

    // 2. Worker 2 立即尝试（锁未过期）
    acquired2, _, _ := consumer2.tryBeginInbox(ctx, "evt_456")
    assert.False(t, acquired2) // 应该失败

    // 3. 等待锁过期
    time.Sleep(2*time.Minute + 1*time.Second)

    // 4. Worker 2 再次尝试（锁已过期）
    acquired3, _, _ := consumer2.tryBeginInbox(ctx, "evt_456")
    assert.True(t, acquired3) // 应该成功
}
```

## 📈 性能影响

### 消息重新投递

**问题**: 不提交 offset 会导致消息重新投递，增加处理次数

**缓解措施**:
1. 合理设置锁超时时间（默认 2 分钟）
2. Kafka 消费者配置 `session.timeout.ms` 和 `max.poll.interval.ms`
3. 监控重复投递率

### 推荐配置

```go
// Kafka 消费者配置
cfg.Consumer.Group.Session.Timeout = 30 * time.Second  // 会话超时
cfg.Consumer.MaxProcessingTime = 5 * time.Minute       // 最大处理时间

// Inbox 锁超时
lockUntil := now.Add(2 * time.Minute)  // 锁超时时间
```

**原则**: `Inbox 锁超时` < `Kafka MaxProcessingTime`

## 🔍 监控指标

### 新增日志

```
[NewFileUploadConsumer]inbox locked, will not commit offset
  - partition: 分区号
  - offset: 消息 offset

[processMessageWithRetry]Inbox locked, skipping retry
  - offset: 消息 offset
```

### 监控查询

```sql
-- 统计当前被锁定的 Inbox 记录
SELECT COUNT(*) AS locked_count
FROM inboxes
WHERE status = 'PROCESSING'
  AND locked_until > NOW();

-- 统计锁超时的 Inbox 记录（可能有问题）
SELECT COUNT(*) AS expired_lock_count
FROM inboxes
WHERE status = 'PROCESSING'
  AND locked_until < NOW();

-- 查看长时间处理的消息
SELECT event_id, locked_by, locked_until, attempts,
       TIMESTAMPDIFF(MINUTE, updated_at, NOW()) AS processing_minutes
FROM inboxes
WHERE status = 'PROCESSING'
  AND updated_at < DATE_SUB(NOW(), INTERVAL 5 MINUTE)
ORDER BY updated_at ASC;
```

## ⚠️ 注意事项

### 1. 消息重复处理

虽然不会丢失消息，但可能会重复投递。依赖 Inbox 的幂等性保证：
- 第一次处理：插入 Inbox 记录
- 重复投递：检查 Inbox.Status = DONE，跳过处理

### 2. 锁超时设置

锁超时时间应该：
- 大于正常处理时间（避免频繁接管）
- 小于 Kafka 最大处理时间（避免 rebalance）
- 考虑网络延迟和 OSS 上传时间

### 3. 死锁风险

如果 Worker 崩溃，锁会一直持有直到超时。

**缓解措施**:
- 设置合理的锁超时时间
- 定期清理过期锁（可选）

```sql
-- 清理过期锁（可选，谨慎使用）
UPDATE inboxes
SET locked_by = '', locked_until = NULL
WHERE status = 'PROCESSING'
  AND locked_until < DATE_SUB(NOW(), INTERVAL 10 MINUTE);
```

## 🎯 最佳实践

### 1. 合理配置超时时间

```go
// Inbox 锁超时：2 分钟
lockUntil := now.Add(2 * time.Minute)

// Kafka 配置
cfg.Consumer.Group.Session.Timeout = 30 * time.Second
cfg.Consumer.MaxProcessingTime = 5 * time.Minute
```

### 2. 监控告警

设置告警规则：
- 锁超时的 Inbox 记录 > 10 条
- 单条消息处理时间 > 5 分钟
- Inbox 重复投递率 > 10%

### 3. 日志分析

定期分析日志，识别问题：
```bash
# 统计 Inbox 锁定次数
grep "inbox locked, will not commit offset" store_srv.log | wc -l

# 查看锁定的消息
grep "inbox locked" store_srv.log | tail -20
```

## 📝 相关文档

- `INBOX_DLQ_QUICK_REF.md` - Inbox-DLQ 同步机制
- `DLQ_README.md` - DLQ 系统完整文档
- `DLQ_OSS_CHECK.md` - DLQ OSS 状态检查

## ✨ 总结

这个修复确保了：
1. ✅ **消息不丢失** - 锁定时不提交 offset
2. ✅ **幂等性保证** - 依赖 Inbox 机制
3. ✅ **自动恢复** - 锁过期后自动接管
4. ✅ **性能可控** - 合理配置超时时间

修复后的系统更加健壮，避免了消息丢失的严重问题。
