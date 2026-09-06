#include "MysqlAccountRepository.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include <utility>
#include <memory>

namespace core::infrastructure::mysql
{

void MysqlAccountRepository::getUserInfo(const core::domain::RequestContext &ctx, UserInfoCallback callback)
{
	auto callbackPtr = std::make_shared<UserInfoCallback>(std::move(callback));
	if (!ctx.authenticated())
	{
		(*callbackPtr)(core::domain::Result<core::application::account::UserInfo>::fail(
			core::domain::ErrorCode::Unauthenticated,
			"missing authenticated request context"));
		return;
	}
	if (!dbClient_)
	{
		(*callbackPtr)(core::domain::Result<core::application::account::UserInfo>::fail(
			core::domain::ErrorCode::DependencyUnavailable,
			"mysql client is not available"));
		return;
    }

	dbClient_->execSqlAsync(
		"select id, name, email, mobile, gender from accounts where id=? and name=? and deleted_at is null limit 1",
		[callbackPtr](const drogon::orm::Result &rows) {
			if (rows.empty())
			{
				(*callbackPtr)(core::domain::Result<core::application::account::UserInfo>::fail(
					core::domain::ErrorCode::NotFound,
					"account not found"));
				return;
            }

            const auto row = rows[0];
            core::application::account::UserInfo info;
            info.id = row["id"].as<std::uint64_t>();
            info.username = row["name"].as<std::string>();
			info.email = row["email"].isNull() ? "" : row["email"].as<std::string>();
			info.mobile = row["mobile"].isNull() ? "" : row["mobile"].as<std::string>();
			info.gender = row["gender"].isNull() ? "" : row["gender"].as<std::string>();
			(*callbackPtr)(core::domain::Result<core::application::account::UserInfo>::ok(std::move(info)));
		},
		[callbackPtr](const drogon::orm::DrogonDbException &e) {
			(*callbackPtr)(core::domain::Result<core::application::account::UserInfo>::fail(
				core::domain::ErrorCode::Internal,
				e.base().what()));
		},
        static_cast<unsigned long long>(ctx.userId.value()),
        ctx.username);
}

void MysqlAccountRepository::findCredentialsByUsername(const std::string &username, CredentialsCallback callback)
{
	auto callbackPtr = std::make_shared<CredentialsCallback>(std::move(callback));
	if (username.empty())
	{
		(*callbackPtr)(core::domain::Result<core::application::account::AccountCredentials>::fail(
			core::domain::ErrorCode::InvalidArgument,
			"username is required"));
		return;
	}
	if (!dbClient_)
	{
		(*callbackPtr)(core::domain::Result<core::application::account::AccountCredentials>::fail(
			core::domain::ErrorCode::DependencyUnavailable,
			"mysql client is not available"));
		return;
	}

	dbClient_->execSqlAsync(
		"select id, name, password, salt from accounts where name=? and deleted_at is null limit 1",
		[callbackPtr](const drogon::orm::Result &rows) {
			if (rows.empty())
			{
				(*callbackPtr)(core::domain::Result<core::application::account::AccountCredentials>::fail(
					core::domain::ErrorCode::NotFound,
					"account not found"));
				return;
			}

			const auto row = rows[0];
			core::application::account::AccountCredentials credentials;
			credentials.id = row["id"].as<std::uint64_t>();
			credentials.username = row["name"].as<std::string>();
			credentials.passwordHash = row["password"].as<std::string>();
			credentials.salt = row["salt"].as<std::string>();
			(*callbackPtr)(core::domain::Result<core::application::account::AccountCredentials>::ok(std::move(credentials)));
		},
		[callbackPtr](const drogon::orm::DrogonDbException &e) {
			(*callbackPtr)(core::domain::Result<core::application::account::AccountCredentials>::fail(
				core::domain::ErrorCode::Internal,
				e.base().what()));
		},
		username);
}

void MysqlAccountRepository::accountExistsByEmail(const std::string &email, ExistsCallback callback)
{
	auto callbackPtr = std::make_shared<ExistsCallback>(std::move(callback));
	if (email.empty())
	{
		(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::InvalidArgument, "email is required"));
		return;
	}
	if (!dbClient_)
	{
		(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::DependencyUnavailable, "mysql client is not available"));
		return;
	}
	dbClient_->execSqlAsync(
		"select id from accounts where email=? and deleted_at is null limit 1",
		[callbackPtr](const drogon::orm::Result &rows) {
			(*callbackPtr)(core::domain::Result<bool>::ok(!rows.empty()));
		},
		[callbackPtr](const drogon::orm::DrogonDbException &e) {
			(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::Internal, e.base().what()));
		},
		email);
}

void MysqlAccountRepository::accountExistsByUsername(const std::string &username, ExistsCallback callback)
{
	auto callbackPtr = std::make_shared<ExistsCallback>(std::move(callback));
	if (username.empty())
	{
		(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::InvalidArgument, "username is required"));
		return;
	}
	if (!dbClient_)
	{
		(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::DependencyUnavailable, "mysql client is not available"));
		return;
	}
	dbClient_->execSqlAsync(
		"select id from accounts where name=? and deleted_at is null limit 1",
		[callbackPtr](const drogon::orm::Result &rows) {
			(*callbackPtr)(core::domain::Result<bool>::ok(!rows.empty()));
		},
		[callbackPtr](const drogon::orm::DrogonDbException &e) {
			(*callbackPtr)(core::domain::Result<bool>::fail(core::domain::ErrorCode::Internal, e.base().what()));
		},
		username);
}

void MysqlAccountRepository::createAccountFromPendingRegistration(
	const core::application::account::PendingRegistration &pending,
	CreateAccountCallback callback)
{
	auto callbackPtr = std::make_shared<CreateAccountCallback>(std::move(callback));
	if (pending.username.empty() || pending.email.empty() || pending.passwordHash.empty() || pending.salt.empty())
	{
		(*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
												 "pending registration is incomplete"));
		return;
	}
	if (!dbClient_)
	{
		(*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
												 "mysql client is not available"));
		return;
	}

	dbClient_->execSqlAsync(
		"insert into accounts (created_at, updated_at, mobile, password, nickname, name, gender, role, salt, email) "
		"values (now(), now(), '', ?, '', ?, 'male', 1, ?, ?)",
		[callbackPtr](const drogon::orm::Result &) {
			(*callbackPtr)(core::domain::Result<void>::success());
		},
		[callbackPtr](const drogon::orm::DrogonDbException &e) {
			(*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Conflict, e.base().what()));
		},
		pending.passwordHash,
		pending.username,
		pending.salt,
		pending.email);
}

} // namespace core::infrastructure::mysql
