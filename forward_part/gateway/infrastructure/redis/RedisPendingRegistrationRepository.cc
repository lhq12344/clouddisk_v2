#include "RedisPendingRegistrationRepository.h"

#include <jsoncpp/json/json.h>

#include <memory>
#include <utility>

namespace core::infrastructure::redis
{
namespace
{
std::string pendingKey(const std::string &email)
{
    return "pending_reg:" + email;
}

std::string pendingToJson(const core::application::account::PendingRegistration &pending)
{
    Json::Value value;
    value["username"] = pending.username;
    value["email"] = pending.email;
    value["password"] = pending.passwordHash;
    value["salt"] = pending.salt;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

core::domain::Result<core::application::account::PendingRegistration> parsePending(const std::string &payload)
{
    Json::Value value;
    Json::CharReaderBuilder builder;
    std::string errors;
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(payload.data(), payload.data() + payload.size(), &value, &errors) || !value.isObject())
    {
        return core::domain::Result<core::application::account::PendingRegistration>::fail(
            core::domain::ErrorCode::Internal,
            "invalid pending registration payload");
    }
    core::application::account::PendingRegistration pending;
    pending.username = value.get("username", "").asString();
    pending.email = value.get("email", "").asString();
    pending.passwordHash = value.get("password", "").asString();
    pending.salt = value.get("salt", "").asString();
    if (pending.username.empty() || pending.email.empty() || pending.passwordHash.empty() || pending.salt.empty())
    {
        return core::domain::Result<core::application::account::PendingRegistration>::fail(
            core::domain::ErrorCode::Internal,
            "pending registration payload is incomplete");
    }
    return core::domain::Result<core::application::account::PendingRegistration>::ok(std::move(pending));
}
} // namespace

void RedisPendingRegistrationRepository::storePendingRegistration(const core::application::account::PendingRegistration &pending,
                                                                  StoreCallback callback)
{
    auto callbackPtr = std::make_shared<StoreCallback>(std::move(callback));
    if (!redisClient_)
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                        "redis client is not available"));
        return;
    }
    const auto payload = pendingToJson(pending);
    redisClient_->execCommandAsync(
        [callbackPtr](const drogon::nosql::RedisResult &) {
            (*callbackPtr)(core::domain::Result<void>::success());
        },
        [callbackPtr](const std::exception &e) {
            (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Internal, e.what()));
        },
        "SET %s %s EX 600", pendingKey(pending.email).c_str(), payload.c_str());
}

void RedisPendingRegistrationRepository::loadPendingRegistration(const std::string &email, LoadCallback callback)
{
    auto callbackPtr = std::make_shared<LoadCallback>(std::move(callback));
    if (!redisClient_)
    {
        (*callbackPtr)(core::domain::Result<core::application::account::PendingRegistration>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "redis client is not available"));
        return;
    }
    redisClient_->execCommandAsync(
        [callbackPtr](const drogon::nosql::RedisResult &result) {
            if (result.type() == drogon::nosql::RedisResultType::kNil)
            {
                (*callbackPtr)(core::domain::Result<core::application::account::PendingRegistration>::fail(
                    core::domain::ErrorCode::NotFound,
                    "no pending registration, please sign up first"));
                return;
            }
            (*callbackPtr)(parsePending(result.asString()));
        },
        [callbackPtr](const std::exception &e) {
            (*callbackPtr)(core::domain::Result<core::application::account::PendingRegistration>::fail(
                core::domain::ErrorCode::Internal,
                e.what()));
        },
        "GET %s", pendingKey(email).c_str());
}

void RedisPendingRegistrationRepository::loadVerificationCode(const std::string &email, CodeCallback callback)
{
    auto callbackPtr = std::make_shared<CodeCallback>(std::move(callback));
    if (!redisClient_)
    {
        (*callbackPtr)(core::domain::Result<std::string>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                               "redis client is not available"));
        return;
    }
    redisClient_->execCommandAsync(
        [callbackPtr](const drogon::nosql::RedisResult &result) {
            if (result.type() == drogon::nosql::RedisResultType::kNil)
            {
                (*callbackPtr)(core::domain::Result<std::string>::fail(core::domain::ErrorCode::NotFound,
                                                                       "Have no find email key"));
                return;
            }
            (*callbackPtr)(core::domain::Result<std::string>::ok(result.asString()));
        },
        [callbackPtr](const std::exception &e) {
            (*callbackPtr)(core::domain::Result<std::string>::fail(core::domain::ErrorCode::Internal, e.what()));
        },
        "GET %s", email.c_str());
}

void RedisPendingRegistrationRepository::deletePendingRegistrationAndCode(const std::string &email, StoreCallback callback)
{
    auto callbackPtr = std::make_shared<StoreCallback>(std::move(callback));
    if (!redisClient_)
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                        "redis client is not available"));
        return;
    }
    redisClient_->execCommandAsync(
        [callbackPtr](const drogon::nosql::RedisResult &) {
            (*callbackPtr)(core::domain::Result<void>::success());
        },
        [callbackPtr](const std::exception &e) {
            (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Internal, e.what()));
        },
        "DEL %s %s", pendingKey(email).c_str(), email.c_str());
}

} // namespace core::infrastructure::redis
