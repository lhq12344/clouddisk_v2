#include "MysqlOutboxRepository.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include <memory>
#include <utility>

namespace core::infrastructure::outbox
{

void MysqlOutboxRepository::enqueue(const core::application::OutboxMessage &message, EnqueueCallback callback)
{
    auto callbackPtr = std::make_shared<EnqueueCallback>(std::move(callback));
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                        "mysql client is not available"));
        return;
    }
    if (message.eventId.empty() || message.txId.empty() || message.eventType.empty() || message.topic.empty() || message.payload.empty())
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                        "outbox message is incomplete"));
        return;
    }

    dbClient_->execSqlAsync(
        "insert into outboxes (created_at, updated_at, event_id, tx_id, event_type, topic, `key`, payload, headers, status, retry_count, next_retry_at, last_error, locked_by) "
        "values (now(), now(), ?, ?, ?, ?, ?, ?, ?, 'NEW', 0, now(), '', '') "
        "on duplicate key update updated_at=updated_at",
        [callbackPtr](const drogon::orm::Result &) {
            (*callbackPtr)(core::domain::Result<void>::success());
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Internal, e.base().what()));
        },
        message.eventId,
        message.txId,
        message.eventType,
        message.topic,
        message.key,
        message.payload,
        message.headers);
}

} // namespace core::infrastructure::outbox
