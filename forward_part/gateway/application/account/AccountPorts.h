#pragma once

#include "../../domain/common/RequestContext.h"
#include "../../domain/common/Result.h"

#include <cstdint>
#include <functional>
#include <string>

namespace core::application::account
{

struct UserInfo
{
    std::uint64_t id{};
    std::string username;
    std::string email;
    std::string mobile;
    std::string gender;
};

struct AccountCredentials
{
    std::uint64_t id{};
    std::string username;
    std::string passwordHash;
    std::string salt;
};

struct PendingRegistration
{
    std::string username;
    std::string email;
    std::string passwordHash;
    std::string salt;
};

class AccountReadPort
{
public:
    virtual ~AccountReadPort() = default;
    using UserInfoCallback = std::function<void(core::domain::Result<UserInfo>)>;
    using CredentialsCallback = std::function<void(core::domain::Result<AccountCredentials>)>;
    using ExistsCallback = std::function<void(core::domain::Result<bool>)>;
    virtual void getUserInfo(const core::domain::RequestContext &ctx, UserInfoCallback callback) = 0;
    virtual void findCredentialsByUsername(const std::string &username, CredentialsCallback callback) = 0;
    virtual void accountExistsByEmail(const std::string &email, ExistsCallback callback) = 0;
    virtual void accountExistsByUsername(const std::string &username, ExistsCallback callback) = 0;
};

class AccountWritePort
{
public:
    virtual ~AccountWritePort() = default;
    using CreateAccountCallback = std::function<void(core::domain::Result<void>)>;
    virtual void createAccountFromPendingRegistration(const PendingRegistration &pending, CreateAccountCallback callback) = 0;
};

class PasswordVerifier
{
public:
    virtual ~PasswordVerifier() = default;
    virtual bool verifyLegacyPassword(const std::string &rawPassword,
                                      const std::string &salt,
                                      const std::string &encodedPassword) const = 0;
};

class PasswordHasher
{
public:
    virtual ~PasswordHasher() = default;
    virtual core::domain::Result<PendingRegistration> hashPendingRegistration(const std::string &username,
                                                                              const std::string &email,
                                                                              const std::string &rawPassword) const = 0;
};

class PendingRegistrationPort
{
public:
    virtual ~PendingRegistrationPort() = default;
    using StoreCallback = std::function<void(core::domain::Result<void>)>;
    using LoadCallback = std::function<void(core::domain::Result<PendingRegistration>)>;
    using CodeCallback = std::function<void(core::domain::Result<std::string>)>;

    virtual void storePendingRegistration(const PendingRegistration &pending, StoreCallback callback) = 0;
    virtual void loadPendingRegistration(const std::string &email, LoadCallback callback) = 0;
    virtual void loadVerificationCode(const std::string &email, CodeCallback callback) = 0;
    virtual void deletePendingRegistrationAndCode(const std::string &email, StoreCallback callback) = 0;
};

class JwtIssuer
{
public:
    virtual ~JwtIssuer() = default;
    virtual core::domain::Result<std::string> issueSigninToken(std::uint64_t userId, const std::string &username) const = 0;
};

} // namespace core::application::account
