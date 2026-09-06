#pragma once

#include "../../application/account/AccountPorts.h"

#include <drogon/nosql/RedisClient.h>

#include <utility>

namespace core::infrastructure::redis
{

class RedisPendingRegistrationRepository final : public core::application::account::PendingRegistrationPort
{
public:
    explicit RedisPendingRegistrationRepository(drogon::nosql::RedisClientPtr redisClient) : redisClient_(std::move(redisClient)) {}

    void storePendingRegistration(const core::application::account::PendingRegistration &pending, StoreCallback callback) override;
    void loadPendingRegistration(const std::string &email, LoadCallback callback) override;
    void loadVerificationCode(const std::string &email, CodeCallback callback) override;
    void deletePendingRegistrationAndCode(const std::string &email, StoreCallback callback) override;

private:
    drogon::nosql::RedisClientPtr redisClient_;
};

} // namespace core::infrastructure::redis
