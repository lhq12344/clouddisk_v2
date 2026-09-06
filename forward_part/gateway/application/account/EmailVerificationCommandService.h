#pragma once

#include "../common/OutboxPort.h"
#include "../common/FeatureFlags.h"

#include <chrono>
#include <sstream>
#include <utility>

namespace core::application::account
{

struct SendVerificationCodeCommand
{
    std::string email;
    std::string requestId;
};

class EmailVerificationCommandService
{
public:
    using Callback = std::function<void(core::domain::Result<void>)>;

    EmailVerificationCommandService(core::application::OutboxPort &outbox, CoreFeatureFlags flags)
        : outbox_(outbox), flags_(flags) {}

    bool shouldUseCoreCommandPath() const
    {
        return flags_.accountRegistration == ImplementationRoute::Core;
    }

    void sendVerificationCode(SendVerificationCodeCommand command, Callback callback)
    {
        if (command.email.empty())
        {
            callback(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument, "Email is empty"));
            return;
        }

        core::application::OutboxMessage message;
        message.eventType = "EMAIL_VERIFICATION_REQUESTED";
        message.topic = "email_verify";
        message.key = command.email;
        message.txId = "email_verify:" + command.email;
        message.eventId = message.txId + ":" + (command.requestId.empty()
                                                    ? std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                                                         std::chrono::system_clock::now().time_since_epoch())
                                                                         .count())
                                                    : command.requestId);
        message.payload = "{\"event_type\":\"EMAIL_VERIFICATION_REQUESTED\",\"email\":\"" + escapeJson(command.email) + "\"}";
        if (!command.requestId.empty())
        {
            message.headers = "{\"x-request-id\":\"" + escapeJson(command.requestId) + "\"}";
        }
        outbox_.enqueue(message, std::move(callback));
    }

private:
    static std::string escapeJson(const std::string &value)
    {
        std::ostringstream out;
        for (const auto ch : value)
        {
            switch (ch)
            {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
            }
        }
        return out.str();
    }

    core::application::OutboxPort &outbox_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::account
