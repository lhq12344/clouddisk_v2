#pragma once

#include "../../domain/common/Result.h"

#include <functional>
#include <string>

namespace core::application
{

struct OutboxMessage
{
    std::string eventId;
    std::string txId;
    std::string eventType;
    std::string topic;
    std::string key;
    std::string payload;
    std::string headers{"{}"};
};

class OutboxPort
{
public:
    virtual ~OutboxPort() = default;
    using EnqueueCallback = std::function<void(core::domain::Result<void>)>;
    virtual void enqueue(const OutboxMessage &message, EnqueueCallback callback) = 0;
};

} // namespace core::application
