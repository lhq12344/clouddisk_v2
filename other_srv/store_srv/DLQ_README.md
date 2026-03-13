# 死信队列（DLQ）系统使用文档

## 概述

死信队列（Dead Letter Queue, DLQ）系统用于处理 Kafka 消费过程中失败的消息。当消息处理失败且重试多次后仍然失败时，消息会被发送到 DLQ，由专门的消费者以较低频率处理。

## 架构设计

```
主队列 (file.upload.cmd)
    │
    ├─> 消费成功 ──> 提交 offset
    │
    └─> 消费失败
         │
         ├─> 可重试错误 ──> 指数退避重试（最多 3 次）
         │    │
         │    ├─> 重试成功 ──> 提交 offset
         │    └─> 重试失败 ──> 发送到 DLQ
         │
         └─> 不可重试错误 ──> 直接发送到 DLQ

DLQ (file.upload.cmd.dlq)
    │
    ├─> DLQ Consumer（每 5 分钟轮询一次）
    │    │
    │    ├─> 处理成功 ──> 标记为 resolved
    │    └─> 处理失败 ──> 标记为 failed + 告警
    │
    └─> MySQL 持久化（dlq_failures 表）
         │
         └─> 管理 API（查询/重试/删除）
```

## 核心组件

### 1. 重试机制

**配置：**

- 最大重试次数：3 次
- 初始退避时间：1 秒
- 最大退避时间：30 秒
- 退避倍数：2.0（指数退避）

**退避时间计算：**

- 第 1 次重试：1 秒
- 第 2 次重试：2 秒
- 第 3 次重试：4 秒

### 2. 错误分类

**可重试错误（Retryable）：**

- 网络超时
- 服务不可用
- 限流错误
- 连接重置
- OSS 连接超时

**不可重试错误（Non-Retryable）：**

- 无效文件格式
- 文件未找到
- 认证失败
- 无效对象键
- 文件过大
- 无效 payload
- 重复事件

### 3. DLQ Producer

**功能：**

- 发送失败消息到 DLQ topic
- 记录失败元数据（原因、次数、堆栈等）
- 持久化到 MySQL

**消息结构：**

```json
{
  "original_topic": "file.upload.cmd",
  "original_partition": 0,
  "original_offset": 12345,
  "original_key": "event_123",
  "original_value": "...",
  "failure_reason": "network timeout",
  "failure_count": 3,
  "error_category": "retryable",
  "error_stack": "...",
  "first_failed_at": "2026-03-04T10:00:00Z",
  "last_failed_at": "2026-03-04T10:00:08Z",
  "user_id": "1001",
  "file_hash": "abc123",
  "object_key": "u/1001/file.bin",
  "event_id": "evt_123"
}
```

### 4. DLQ Consumer

**特点：**

- 低频轮询（默认 5 分钟）
- 批量处理（每次 10 条）
- 自动重试（最多 1 次）
- 失败告警

**处理流程：**

1. 从 MySQL 查询 `status=pending` 的消息
2. 更新状态为 `retrying`
3. 重新处理消息
4. 成功：标记为 `resolved`
5. 失败：标记为 `failed` + 发送告警

### 5. DLQ Manager

**管理 API：**

- `ListDLQMessages` - 查询 DLQ 消息列表
- `GetDLQMessage` - 获取单个消息详情
- `RetryMessage` - 手动重试单条消息
- `RetryBatch` - 批量重试消息
- `MarkResolved` - 手动标记为已解决
- `DeleteMessage` - 删除消息
- `DeleteBatch` - 批量删除消息
- `CleanupOldMessages` - 清理旧消息
- `GetStatistics` - 获取统计信息

## 数据库表结构

### dlq_failures 表

| 字段            | 类型         | 说明         |
| --------------- | ------------ | ------------ |
| id              | BIGINT       | 主键         |
| topic           | VARCHAR(255) | 原始 topic   |
| partition       | INT          | 原始分区     |
| offset          | BIGINT       | 原始 offset  |
| user_id         | VARCHAR(64)  | 用户 ID      |
| object_key      | VARCHAR(512) | OSS 对象键   |
| file_hash       | VARCHAR(128) | 文件哈希     |
| event_id        | VARCHAR(64)  | 事件 ID      |
| original_key    | VARCHAR(255) | 原始消息 key |
| original_value  | MEDIUMBLOB   | 原始消息内容 |
| failure_reason  | TEXT         | 失败原因     |
| failure_count   | INT          | 失败次数     |
| error_category  | VARCHAR(32)  | 错误类型     |
| error_stack     | TEXT         | 错误堆栈     |
| first_failed_at | TIMESTAMP    | 首次失败时间 |
| last_failed_at  | TIMESTAMP    | 最后失败时间 |
| status          | VARCHAR(16)  | 状态         |
| resolved_at     | TIMESTAMP    | 解决时间     |
| resolved_by     | VARCHAR(128) | 解决人/系统  |
| resolution      | TEXT         | 解决方案说明 |

**状态枚举：**

- `pending` - 待处理
- `retrying` - 重试中
- `failed` - 最终失败
- `resolved` - 已解决

## 使用指南

### 1. 初始化数据库

```bash
mysql -u root -p clouddisk < scripts/dlq_migration.sql
```

### 2. 配置 Kafka Topic

创建 DLQ topic：

```bash
kafka-topics.sh --create \
  --bootstrap-server localhost:31092 \
  --topic file.upload.cmd.dlq \
  --partitions 3 \
  --replication-factor 1
```

### 3. 启动服务

```bash
cd other_srv/store_srv
go run main.go
```

**日志输出：**

```
DLQ producer created successfully
Main consumer created successfully
DLQ consumer started (poll interval: 5m0s)
DLQ manager created successfully
Store service started successfully
Main topic: file.upload.cmd, DLQ topic: file.upload.cmd.dlq
```

### 4. 查询 DLQ 消息

**查询所有待处理消息：**

```go
filter := kafka.DLQFilter{
    Status: &model.DLQStatusPending,
    Limit:  20,
}
messages, total, err := dlqManager.ListDLQMessages(ctx, filter)
```

**按用户查询：**

```go
userID := "1001"
filter := kafka.DLQFilter{
    UserID: &userID,
    Limit:  20,
}
messages, total, err := dlqManager.ListDLQMessages(ctx, filter)
```

**按错误类型查询：**

```go
errorCategory := "retryable"
filter := kafka.DLQFilter{
    ErrorCategory: &errorCategory,
    Limit:         20,
}
messages, total, err := dlqManager.ListDLQMessages(ctx, filter)
```

### 5. 手动重试消息

**重试单条消息：**

```go
err := dlqManager.RetryMessage(ctx, messageID)
```

**批量重试：**

```go
ids := []uint{1, 2, 3, 4, 5}
successCount, failCount, err := dlqManager.RetryBatch(ctx, ids)
fmt.Printf("Success: %d, Failed: %d\n", successCount, failCount)
```

### 6. 标记为已解决

```go
err := dlqManager.MarkResolved(ctx, messageID, "手动修复数据后解决", "admin")
```

### 7. 删除消息

```go
// 删除单条
err := dlqManager.DeleteMessage(ctx, messageID)

// 批量删除
ids := []uint{1, 2, 3}
err := dlqManager.DeleteBatch(ctx, ids)
```

### 8. 查看统计信息

```go
stats, err := dlqManager.GetStatistics(ctx)
fmt.Printf("Statistics: %+v\n", stats)
```

**输出示例：**

```json
{
  "by_status": {
    "pending": 15,
    "retrying": 2,
    "failed": 5,
    "resolved": 100
  },
  "by_error_category": {
    "retryable": 10,
    "non_retryable": 8,
    "unknown": 4
  },
  "failures_last_24h": 22,
  "recoveries_last_24h": 18,
  "recovery_rate": 81.82
}
```

### 9. 查看 DLQ 指标

```go
metrics, err := dlqConsumer.GetMetrics(ctx)
fmt.Printf("DLQ Metrics: %+v\n", metrics)
```

**输出示例：**

```json
{
  "dlq_message_count": 122,
  "dlq_pending_count": 15,
  "dlq_retrying_count": 2,
  "dlq_failed_count": 5,
  "dlq_resolved_count": 100,
  "dlq_growth_rate": 2.5,
  "dlq_recovery_rate": 1.8,
  "retryable_errors": 10,
  "non_retryable_errors": 8,
  "unknown_errors": 4,
  "last_updated": "2026-03-04T10:30:00Z"
}
```

## 监控和告警

### 关键指标

1. **DLQ 消息总数** - 应该保持在低水平
2. **DLQ 增长率** - 每分钟新增消息数
3. **DLQ 恢复率** - 每分钟恢复消息数
4. **错误率** - 主队列的错误率
5. **待处理消息数** - `status=pending` 的消息数

### 告警规则

**告警条件：**

- DLQ 消息总数 > 100
- DLQ 增长率 > 10 msg/min
- 错误率 > 5%
- 待处理消息数 > 50

**告警级别：**

- **Warning** - DLQ 消息数超过阈值
- **Critical** - DLQ 消息最终失败（`status=failed`）

## 最佳实践

### 1. 错误分类

正确分类错误类型，避免不可重试错误浪费重试次数：

```go
// 示例：OSS 上传错误处理
if err := ossClient.Upload(data); err != nil {
    if strings.Contains(err.Error(), "timeout") {
        return fmt.Errorf("%w: %v", kafka.ErrOSSConnectionTimeout, err)
    }
    if strings.Contains(err.Error(), "invalid key") {
        return fmt.Errorf("%w: %v", kafka.ErrInvalidObjectKey, err)
    }
    return err
}
```

### 2. 幂等性保证

使用 Inbox 模式确保消息处理的幂等性，避免重复处理：

```go
// 1. 尝试获取 Inbox 锁
acquired, done, err := tryBeginInbox(ctx, eventID)
if done {
    return nil // 已处理过
}
if !acquired {
    return ErrInboxLocked // 其他实例正在处理
}

// 2. 处理消息
err = processMessage(msg)

// 3. 更新 Inbox 状态
if err == nil {
    markInboxDone(ctx, eventID)
} else {
    markInboxFailed(ctx, eventID, err.Error())
}
```

### 3. 定期清理

定期清理已解决的旧消息，避免表膨胀：

```go
// 每天清理一次，保留 30 天
deleted, err := dlqManager.CleanupOldMessages(ctx, 30)
```

### 4. 监控 DLQ 堆积

如果 DLQ 持续堆积，说明系统存在问题：

- 检查错误日志，找出根本原因
- 调整重试策略
- 优化业务逻辑
- 增加资源（如 OSS 带宽）

### 5. 手动介入

对于 `status=failed` 的消息，需要人工介入：

1. 查看失败原因和堆栈
2. 修复根本问题（如修复数据、调整配置）
3. 手动重试或标记为已解决

## 故障排查

### 问题 1：DLQ 消息持续增长

**原因：**

- 下游服务（OSS）不可用
- 网络问题
- 配置错误

**解决：**

1. 检查 OSS 服务状态
2. 检查网络连接
3. 查看错误日志，定位具体原因

### 问题 2：DLQ Consumer 不工作

**原因：**

- DLQ topic 不存在
- 消费者配置错误
- 数据库连接失败

**解决：**

1. 检查 Kafka topic 是否存在
2. 检查配置文件
3. 检查数据库连接

### 问题 3：消息重复处理

**原因：**

- Inbox 幂等性失效
- 事务未正确提交

**解决：**

1. 检查 Inbox 表的唯一索引
2. 确保事务正确提交
3. 检查锁超时时间

## 性能优化

### 1. 批量处理

DLQ Consumer 使用批量处理，减少数据库查询次数：

```go
config := kafka.DLQConsumerConfig{
    BatchSize: 20, // 每次处理 20 条
}
```

### 2. 调整轮询间隔

根据 DLQ 消息量调整轮询间隔：

```go
config := kafka.DLQConsumerConfig{
    PollInterval: 1 * time.Minute, // 高频：1 分钟
    // PollInterval: 5 * time.Minute, // 低频：5 分钟
}
```

### 3. 并发处理

主消费者使用协程池并发处理消息：

```go
handler := kafka.NewFileUploadConsumer(ctx, 20, dlqProducer) // 20 个 worker
```

## 总结

死信队列系统提供了完整的失败消息处理机制：

✅ **自动重试** - 指数退避，智能重试
✅ **错误分类** - 区分可重试和不可重试错误
✅ **持久化** - MySQL 持久化，防止消息丢失
✅ **低频处理** - DLQ Consumer 低频轮询，避免资源浪费
✅ **管理 API** - 完整的查询、重试、删除接口
✅ **监控告警** - 实时指标，及时发现问题
✅ **幂等性** - Inbox 模式，避免重复处理

通过合理使用 DLQ 系统，可以大大提高系统的可靠性和可维护性。
