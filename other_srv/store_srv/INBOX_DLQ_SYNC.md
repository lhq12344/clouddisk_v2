# Inbox 与 DLQ 状态同步机制

## 概述

当消息处理失败并进入死信队列（DLQ）时，Inbox 表中的对应记录需要同步更新状态，以保持数据一致性。

## Inbox 状态定义

```go
type InboxStatus string

const (
    InboxProcessing InboxStatus = "PROCESSING" // 处理中（持有锁）
    InboxDone       InboxStatus = "DONE"       // 已完成
    InboxFailed     InboxStatus = "FAILED"     // 失败（可重试，未进 DLQ）
    InboxDLQ        InboxStatus = "DLQ"        // 已进入死信队列
)
```

## 状态流转图

```
消息到达
    ↓
创建 Inbox (status=PROCESSING, locked_by=instance_id)
    ↓
处理消息
    ├─ 成功 → status=DONE, locked_by=null
    │
    ├─ 失败（可重试）→ 重试 3 次
    │   ├─ 重试成功 → status=DONE, locked_by=null
    │   └─ 重试失败 → 进入 DLQ
    │       ↓
    │       status=DLQ, locked_by=null, dlq_failure_id=123
    │
    └─ 失败（不可重试）→ 直接进入 DLQ
        ↓
        status=DLQ, locked_by=null, dlq_failure_id=123

DLQ 处理
    ├─ 成功恢复 → status=DONE, dlq_failure_id 保留（用于追溯）
    └─ 最终失败 → status=DLQ, dlq_failure_id 保留
```

## 关键设计决策

### 1. 为什么要释放锁？

**问题：** 消息进入 DLQ 后，Inbox 记录仍然持有锁（`locked_by` 和 `locked_until`），会导致：
- 锁超时后，其他实例可能尝试重新处理（但消息已在 DLQ）
- 锁资源浪费
- 状态不清晰

**解决：** 进入 DLQ 时释放锁：
```go
Updates(map[string]interface{}{
    "status":       model.InboxDLQ,
    "locked_by":    "",    // 释放锁
    "locked_until": nil,   // 清除锁超时
})
```

### 2. 为什么添加 `dlq_failure_id` 字段？

**目的：**
- 关联 Inbox 和 DLQ 记录
- 方便追溯：从 Inbox 可以直接查到 DLQ 详情
- 双向查询：从 DLQ 也可以查到 Inbox 状态

**示例查询：**
```sql
-- 查询某个事件的 DLQ 详情
SELECT i.*, d.*
FROM inbox i
LEFT JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE i.event_id = 'evt_123';

-- 查询所有在 DLQ 中的消息
SELECT * FROM inbox WHERE status = 'DLQ';
```

### 3. 为什么不删除 Inbox 记录？

**原因：**
- 保留完整的处理历史
- 幂等性保证：如果 Kafka 重投，可以通过 Inbox 判断已处理
- 审计追溯：可以查看消息的完整生命周期

### 4. DLQ 恢复后如何处理 Inbox？

**策略：** DLQ 消息成功恢复后，更新 Inbox 状态为 `DONE`

```go
// DLQ Consumer 恢复成功
tx.Model(&model.Inbox{}).
    Where("event_id = ?", eventID).
    Updates(map[string]interface{}{
        "status":     model.InboxDone,
        "last_error": "",
        "updated_at": time.Now(),
    })
```

**注意：** `dlq_failure_id` 保留不变，用于追溯历史。

## 事务保证

所有 Inbox 和 DLQ 的状态更新都在同一个数据库事务中完成，保证原子性。

### 发送到 DLQ 时

```go
func (p *DLQProducer) persistToDB(ctx context.Context, dlqMsg *DLQMessage) error {
    return internal.DB.Transaction(func(tx *gorm.DB) error {
        // 1. 创建 DLQ 记录
        record := model.DLQFailure{...}
        if err := tx.Create(&record).Error; err != nil {
            return err
        }

        // 2. 更新 Inbox 状态
        err := tx.Model(&model.Inbox{}).
            Where("event_id = ?", dlqMsg.EventID).
            Updates(map[string]interface{}{
                "status":         model.InboxDLQ,
                "dlq_failure_id": record.ID,
                "locked_by":      "",
                "locked_until":   nil,
            }).Error

        return err
    })
}
```

### DLQ 恢复时

```go
func (d *DLQConsumer) markResolved(id uint, resolution string) error {
    return internal.DB.Transaction(func(tx *gorm.DB) error {
        // 1. 查询 DLQ 记录
        var failure model.DLQFailure
        if err := tx.First(&failure, id).Error; err != nil {
            return err
        }

        // 2. 更新 DLQ 状态
        now := time.Now()
        if err := tx.Model(&failure).Updates(map[string]interface{}{
            "status":      model.DLQStatusResolved,
            "resolved_at": &now,
        }).Error; err != nil {
            return err
        }

        // 3. 更新 Inbox 状态
        if failure.EventID != "" {
            err := tx.Model(&model.Inbox{}).
                Where("event_id = ?", failure.EventID).
                Updates(map[string]interface{}{
                    "status":     model.InboxDone,
                    "last_error": "",
                }).Error
            if err != nil {
                return err
            }
        }

        return nil
    })
}
```

## 查询示例

### 1. 查询所有在 DLQ 中的消息

```sql
SELECT
    i.event_id,
    i.status AS inbox_status,
    i.attempts,
    i.last_error,
    d.id AS dlq_id,
    d.failure_count,
    d.error_category,
    d.first_failed_at,
    d.status AS dlq_status
FROM inbox i
LEFT JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE i.status = 'DLQ'
ORDER BY d.first_failed_at DESC;
```

### 2. 查询某个用户的失败消息

```sql
SELECT
    i.*,
    d.failure_reason,
    d.error_category,
    d.status AS dlq_status
FROM inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE d.user_id = '1001'
  AND i.status = 'DLQ'
ORDER BY d.first_failed_at DESC;
```

### 3. 统计各状态的消息数

```sql
SELECT
    status,
    COUNT(*) AS count
FROM inbox
GROUP BY status;
```

**输出示例：**
```
+------------+-------+
| status     | count |
+------------+-------+
| PROCESSING |    15 |
| DONE       | 10000 |
| FAILED     |     5 |
| DLQ        |    20 |
+------------+-------+
```

### 4. 查询 DLQ 恢复历史

```sql
SELECT
    i.event_id,
    i.status AS current_status,
    d.status AS dlq_status,
    d.resolved_at,
    d.resolved_by,
    d.resolution
FROM inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE d.status = 'resolved'
ORDER BY d.resolved_at DESC
LIMIT 20;
```

## 监控指标

### 关键指标

1. **Inbox 状态分布**
   ```sql
   SELECT status, COUNT(*) FROM inbox GROUP BY status;
   ```

2. **DLQ 消息数**
   ```sql
   SELECT COUNT(*) FROM inbox WHERE status = 'DLQ';
   ```

3. **平均恢复时间**
   ```sql
   SELECT AVG(TIMESTAMPDIFF(SECOND, d.first_failed_at, d.resolved_at)) AS avg_recovery_seconds
   FROM dlq_failures d
   WHERE d.status = 'resolved'
     AND d.resolved_at IS NOT NULL;
   ```

4. **锁超时的消息**
   ```sql
   SELECT COUNT(*)
   FROM inbox
   WHERE status = 'PROCESSING'
     AND locked_until < NOW();
   ```

## 故障场景处理

### 场景 1：DLQ 发送成功，但 Inbox 更新失败

**原因：** 数据库事务失败或网络问题

**影响：**
- DLQ 中有记录
- Inbox 状态仍为 `PROCESSING` 或 `FAILED`
- 锁可能未释放

**检测：**
```sql
-- 查找 DLQ 中存在但 Inbox 状态不是 DLQ 的记录
SELECT d.event_id, i.status, d.status AS dlq_status
FROM dlq_failures d
LEFT JOIN inbox i ON d.event_id = i.event_id
WHERE i.status != 'DLQ' OR i.status IS NULL;
```

**修复：**
```sql
-- 手动同步状态
UPDATE inbox i
INNER JOIN dlq_failures d ON i.event_id = d.event_id
SET
    i.status = 'DLQ',
    i.dlq_failure_id = d.id,
    i.locked_by = '',
    i.locked_until = NULL
WHERE i.status != 'DLQ';
```

### 场景 2：DLQ 恢复成功，但 Inbox 更新失败

**原因：** 数据库事务失败

**影响：**
- DLQ 状态为 `resolved`
- Inbox 状态仍为 `DLQ`

**检测：**
```sql
SELECT i.event_id, i.status, d.status AS dlq_status
FROM inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE i.status = 'DLQ' AND d.status = 'resolved';
```

**修复：**
```sql
UPDATE inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
SET
    i.status = 'DONE',
    i.last_error = ''
WHERE i.status = 'DLQ' AND d.status = 'resolved';
```

### 场景 3：锁超时但消息在 DLQ

**原因：** 消息进入 DLQ 前锁已超时

**影响：** 其他实例可能尝试重新处理

**检测：**
```sql
SELECT * FROM inbox
WHERE status = 'PROCESSING'
  AND locked_until < NOW()
  AND event_id IN (SELECT event_id FROM dlq_failures);
```

**修复：**
```sql
UPDATE inbox i
INNER JOIN dlq_failures d ON i.event_id = d.event_id
SET
    i.status = 'DLQ',
    i.dlq_failure_id = d.id,
    i.locked_by = '',
    i.locked_until = NULL
WHERE i.status = 'PROCESSING';
```

## 定期维护任务

### 1. 清理已完成的旧记录

```sql
-- 删除 30 天前已完成的 Inbox 记录
DELETE FROM inbox
WHERE status = 'DONE'
  AND updated_at < DATE_SUB(NOW(), INTERVAL 30 DAY);
```

### 2. 同步不一致的状态

```go
// 定期任务：同步 Inbox 和 DLQ 状态
func syncInboxDLQStatus(ctx context.Context) error {
    // 1. DLQ resolved 但 Inbox 不是 DONE
    result := internal.DB.Exec(`
        UPDATE inbox i
        INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
        SET i.status = 'DONE', i.last_error = ''
        WHERE i.status = 'DLQ' AND d.status = 'resolved'
    `)

    log.Printf("Synced %d resolved DLQ messages to Inbox", result.RowsAffected)

    // 2. DLQ 存在但 Inbox 不是 DLQ
    result = internal.DB.Exec(`
        UPDATE inbox i
        INNER JOIN dlq_failures d ON i.event_id = d.event_id
        SET
            i.status = 'DLQ',
            i.dlq_failure_id = d.id,
            i.locked_by = '',
            i.locked_until = NULL
        WHERE i.status != 'DLQ' AND d.status IN ('pending', 'retrying', 'failed')
    `)

    log.Printf("Synced %d DLQ messages to Inbox", result.RowsAffected)

    return nil
}
```

### 3. 释放超时的锁

```sql
-- 释放超时的锁（但不在 DLQ 中的）
UPDATE inbox
SET
    locked_by = '',
    locked_until = NULL,
    status = 'FAILED'
WHERE status = 'PROCESSING'
  AND locked_until < NOW()
  AND event_id NOT IN (SELECT event_id FROM dlq_failures);
```

## 最佳实践

1. **事务保证** - 所有状态更新都在事务中完成
2. **幂等性** - 通过 `event_id` 保证幂等性
3. **锁管理** - 进入 DLQ 时立即释放锁
4. **关联追溯** - 通过 `dlq_failure_id` 关联两张表
5. **定期同步** - 定期检查并修复不一致状态
6. **监控告警** - 监控 DLQ 消息数和状态分布

## 总结

通过引入 `InboxDLQ` 状态和 `dlq_failure_id` 字段，实现了 Inbox 和 DLQ 的完整状态同步：

✅ **释放锁** - 避免锁资源浪费和超时问题
✅ **状态清晰** - 明确标识消息在 DLQ 中
✅ **双向关联** - Inbox ↔ DLQ 可以互相查询
✅ **事务保证** - 原子性更新，避免不一致
✅ **可追溯** - 保留完整的处理历史
✅ **可恢复** - DLQ 恢复后自动同步 Inbox 状态

这种设计确保了系统的数据一致性和可维护性。
