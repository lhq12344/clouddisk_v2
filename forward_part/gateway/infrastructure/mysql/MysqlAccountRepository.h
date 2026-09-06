#pragma once

#include "../../application/account/AccountPorts.h"

#include <drogon/orm/DbClient.h>

#include <utility>

namespace core::infrastructure::mysql
{

class MysqlAccountRepository final : public core::application::account::AccountReadPort,
                                     public core::application::account::AccountWritePort
{
public:
    explicit MysqlAccountRepository(drogon::orm::DbClientPtr dbClient) : dbClient_(std::move(dbClient)) {}

    void getUserInfo(const core::domain::RequestContext &ctx, UserInfoCallback callback) override;
    void findCredentialsByUsername(const std::string &username, CredentialsCallback callback) override;
    void accountExistsByEmail(const std::string &email, ExistsCallback callback) override;
    void accountExistsByUsername(const std::string &username, ExistsCallback callback) override;
    void createAccountFromPendingRegistration(const core::application::account::PendingRegistration &pending,
                                              CreateAccountCallback callback) override;

private:
    drogon::orm::DbClientPtr dbClient_;
};

} // namespace core::infrastructure::mysql
