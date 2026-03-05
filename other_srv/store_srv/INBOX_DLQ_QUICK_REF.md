# Inbox 与 DLQ 状态同步 - 快速参考

## 核心改动

### 1. Inbox 状态新增 `DLQ`

```go
const (
    InboxProcessing InboxStatus = "PROCESSING" // 处理中
    InboxDone       InboxStatus = "DONE"       // 已完成
    InboxFailed     InboxStatus = "FAILED"     // 失败（可重试）
    InboxDLQ        InboxStatus = "DLQ"        // 已进入死信队列 ← 新增
)
```

### 2. Inbox 表新增字段

```go
type Inbox struct {
    // ... 原有字段
    DLQFailureID *uint `gorm:"index;comment:关联的DLQ失败记录ID"` // ← 新增
}
```

### 3. DLQ 消费者新增 OSS 状态检查 ⭐ NEW

在重试之前，先检查文件是否已经成功上传到 OSS：
- ✅ 如果文件已在 OSS → 直接清理 DLQ 记录，更新 Inbox 为 DONE
- ⚠️ 如果文件未在 OSS → 继续执行重试逻辑

详见: `DLQ_OSS_CHECK.md`

## 状态流转

```
消息处理失败 → 重试 3 次 → 仍失败
    ↓
发送到 DLQ (Kafka + MySQL)
    ↓
更新 Inbox:
    - status = DLQ
    - dlq_failure_id = 123
    - locked_by = ""        ← 释放锁
    - locked_until = NULL   ← 清除锁超时
    ↓
DLQ Consumer 轮询处理
    ↓
检查文件是否在 OSS？ ⭐ NEW
    ├─ 是 → 自动清理 DLQ → 更新 Inbox 为 DONE → 结束
    └─ 否 → 继续重试逻辑
              ↓
          重新处理消息
              ↓
          成功恢复？
              ├─ 是 → 更新 Inbox 为 DONE
              └─ 否 → 继续重试或标记为最终失败
```

## 关键代码位置

### 发送到 DLQ 时更新 Inbox

`kafka/dlq_producer.go:persistToDB()`

```go
tx.Model(&model.Inbox{}).
    Where("event_id = ?", dlqMsg.EventID).
    Updates(map[string]interface{}{
        "status":        model.InboxDLQ,
        "dlq_failure_id": record.ID,
        "locked_by":      "",    // 释放锁
        "locked_until":   nil,   // 清除锁超时
    })
```

### Inbox 锁定机制修复 ⭐ NEW

**问题**: 当 Inbox 被其他 worker 锁定时，原逻辑会提交 offset，导致消息丢失

**修复**: 引入 `ErrInboxLocked` 特殊错误，锁定时不提交 offset

`kafka/kafkacunsumer.go`

```go
// 定义特殊错误
var ErrInboxLocked = errors.New("inbox locked by other worker")

// processMessage: 锁定时返回特殊错误
if !acquired {
    return ErrInboxLocked  // 不提交 offset
}

// Worker: 遇到 ErrInboxLocked 不提交 offset
if errors.Is(err, ErrInboxLocked) {
    // 不调用 MarkMessage，让 Kafka 重新投递
} else {
    session.MarkMessage(msg, "")  // 其他情况提交 offset
}
```

详见: `INBOX_LOCK_FIX.md`

**文件：** `kafka/dlq_producer.go`

```go
func (p *DLQProducer) persistToDB(ctx context.Context, dlqMsg *DLQMessage) error {
    return internal.DB.Transaction(func(tx *gorm.DB) error {
        // 1. 创建 DLQ 记录
        record := model.DLQFailure{...}
        tx.Create(&record)

        // 2. 更新 Inbox 状态为 DLQ，释放锁
        tx.Model(&model.Inbox{}).
            Where("event_id = ?", dlqMsg.EventID).
            Updates(map[string]interface{}{
                "status":         model.InboxDLQ,
                "dlq_failure_id": record.ID,
                "locked_by":      "",
                "locked_until":   nil,
            })

        return nil
    })
}
```

### DLQ 恢复时更新 Inbox

**文件：** `kafka/dlq_consumer.go`

```go
func (d *DLQConsumer) markResolved(id uint, resolution string) error {
    return internal.DB.Transaction(func(tx *gorm.DB) error {
        // 1. 更新 DLQ 状态
        tx.Model(&model.DLQFailure{}).
            Where("id = ?", id).
            Updates(map[string]interface{}{
                "status":      model.DLQStatusResolved,
                "resolved_at": time.Now(),
            })

        // 2. 更新 Inbox 状态为 DONE
        tx.Model(&model.Inbox{}).
            Where("event_id = ?", failure.EventID).
            Updates(map[string]interface{}{
                "status":     model.InboxDone,
                "last_error": "",
            })

        return nil
    })
}
```

## 数据库迁移

### 执行顺序

```bash
# 1. 创建 DLQ 表
mysql -u root -p clouddisk < scripts/dlq_migration.sql

# 2. 更新 Inbox 表
mysql -u root -p clouddisk < scripts/inbox_dlq_migration.sql
```

### 迁移内容

```sql
-- inbox_dlq_migration.sql

-- 1. 修改 status 字段，支持 'DLQ' 值
ALTER TABLE `inbox` MODIFY COLUMN `status` VARCHAR(16) NOT NULL;

-- 2. 添加 dlq_failure_id 字段
ALTER TABLE `inbox` ADD COLUMN `dlq_failure_id` BIGINT UNSIGNED DEFAULT NULL;

-- 3. 添加索引
ALTER TABLE `inbox` ADD INDEX `idx_dlq_failure_id` (`dlq_failure_id`);
```

## 常用查询

### 查询所有在 DLQ 中的消息

```sql
SELECT
    i.event_id,
    i.attempts,
    d.failure_count,
    d.error_category,
    d.status AS dlq_status
FROM inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE i.status = 'DLQ';
```

### 查询 DLQ 恢复历史

```sql
SELECT
    i.event_id,
    d.resolved_at,
    d.resolved_by,
    d.resolution
FROM inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
WHERE d.status = 'resolved'
ORDER BY d.resolved_at DESC;
```

### 统计各状态消息数

```sql
SELECT status, COUNT(*) AS count
FROM inbox
GROUP BY status;
```

## 监控指标

```go
// 定期打印 Inbox 状态分布
func printInboxStats(ctx context.Context) {
    var stats []struct {
        Status string
        Count  int64
    }

    internal.DB.Model(&model.Inbox{}).
        Select("status, COUNT(*) as count").
        Group("status").
        Find(&stats)

    for _, s := range stats {
        log.Printf("Inbox[%s]: %d", s.Status, s.Count)
    }
}
```

## 故障恢复

### 同步不一致的状态

```sql
-- 1. DLQ resolved 但 Inbox 不是 DONE
UPDATE inbox i
INNER JOIN dlq_failures d ON i.dlq_failure_id = d.id
SET i.status = 'DONE', i.last_error = ''
WHERE i.status = 'DLQ' AND d.status = 'resolved';

-- 2. DLQ 存在但 Inbox 不是 DLQ
UPDATE inbox i
INNER JOIN dlq_failures d ON i.event_id = d.event_id
SET
    i.status = 'DLQ',
    i.dlq_failure_id = d.id,
    i.locked_by = '',
    i.locked_until = NULL
WHERE i.status != 'DLQ' AND d.status IN ('pending', 'retrying', 'failed');
```

## 为什么这样设计？

### ✅ 释放锁

**问题：** 消息在 DLQ 中，但 Inbox 仍持有锁
**影响：** 锁超时后其他实例可能重复处理
**解决：** 进入 DLQ 时立即释放锁

### ✅ 关联 DLQ

**问题：** 无法追溯消息的失败详情
**影响：** 排查问题困难
**解决：** 通过 `dlq_failure_id` 关联两张表

### ✅ 保留历史

**问题：** 删除 Inbox 记录会丢失处理历史
**影响：** 无法审计，幂等性失效
**解决：** 保留 Inbox 记录，只更新状态

### ✅ 事务保证

**问题：** DLQ 和 Inbox 状态不一致
**影响：** 数据混乱
**解决：** 所有更新在同一事务中完成

## 测试验证

### 1. 测试消息进入 DLQ

```bash
# 启动服务
go run main.go

# 发送一条会失败的消息到 Kafka
# （例如：无效的 OSS key）

# 查看日志
# [DLQProducer] Message sent to DLQ
# [DLQProducer] Updated Inbox status to DLQ

# 查询数据库
mysql> SELECT * FROM inbox WHERE event_id = 'evt_123';
# status = 'DLQ', locked_by = '', dlq_failure_id = 1

mysql> SELECT * FROM dlq_failures WHERE id = 1;
# status = 'pending', event_id = 'evt_123'
```

### 2. 测试 DLQ 恢复

```bash
# 等待 5 分钟（DLQ Consumer 轮询）

# 查看日志
# [DLQConsumer] DLQ message processed successfully
# [DLQConsumer] Updated Inbox status to DONE

# 查询数据库
mysql> SELECT * FROM inbox WHERE event_id = 'evt_123';
# status = 'DONE', dlq_failure_id = 1 (保留)

mysql> SELECT * FROM dlq_failures WHERE id = 1;
# status = 'resolved', resolved_at = '2026-03-04 10:30:00'
```

## 总结

通过引入 `InboxDLQ` 状态和 `dlq_failure_id` 字段，实现了：

✅ **锁管理** - 进入 DLQ 时释放锁，避免资源浪费
✅ **状态同步** - Inbox 和 DLQ 状态保持一致
✅ **双向追溯** - 可以从 Inbox 查 DLQ，也可以从 DLQ 查 Inbox
✅ **事务保证** - 原子性更新，避免不一致
✅ **历史保留** - 完整的处理历史，支持审计

详细文档请参考：`INBOX_DLQ_SYNC.md`
