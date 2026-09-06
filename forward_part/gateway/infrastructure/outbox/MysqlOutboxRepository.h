#pragma once

#include "../../application/common/OutboxPort.h"

#include <drogon/orm/DbClient.h>

#include <utility>

namespace core::infrastructure::outbox
{

class MysqlOutboxRepository final : public core::application::OutboxPort
{
public:
    explicit MysqlOutboxRepository(drogon::orm::DbClientPtr dbClient) : dbClient_(std::move(dbClient)) {}

    void enqueue(const core::application::OutboxMessage &message, EnqueueCallback callback) override;

private:
    drogon::orm::DbClientPtr dbClient_;
};

} // namespace core::infrastructure::outbox
