#pragma once

#include "AccountPorts.h"
#include "../common/FeatureFlags.h"

#include <utility>

namespace core::application::account
{

struct SigninCommand
{
    std::string username;
    std::string password;
};

class SigninService
{
public:
    using Callback = std::function<void(core::domain::Result<std::string>)>;

    SigninService(AccountReadPort &accounts,
                  const PasswordVerifier &passwordVerifier,
                  const JwtIssuer &jwtIssuer,
                  CoreFeatureFlags flags)
        : accounts_(accounts), passwordVerifier_(passwordVerifier), jwtIssuer_(jwtIssuer), flags_(flags) {}

    bool shouldUseCoreLoginPath() const
    {
        return flags_.accountLogin == ImplementationRoute::Core;
    }

    void signin(SigninCommand command, Callback callback)
    {
        if (command.username.empty() || command.password.empty())
        {
            callback(core::domain::Result<std::string>::fail(core::domain::ErrorCode::InvalidArgument,
                                                             "username and password are required"));
            return;
		}

		auto rawPassword = command.password;
		const auto *passwordVerifier = &passwordVerifier_;
		const auto *jwtIssuer = &jwtIssuer_;
		accounts_.findCredentialsByUsername(command.username,
											[passwordVerifier, jwtIssuer, rawPassword = std::move(rawPassword), callback = std::move(callback)](auto accountResult) mutable {
												if (!accountResult.ok())
												{
													callback(core::domain::Result<std::string>::fail(accountResult.error.code, accountResult.error.message));
                                                    return;
												}
												const auto &account = accountResult.value;
												if (!passwordVerifier->verifyLegacyPassword(rawPassword, account.salt, account.passwordHash))
												{
													callback(core::domain::Result<std::string>::fail(core::domain::ErrorCode::Unauthenticated,
																						 "password error"));
													return;
												}
												callback(jwtIssuer->issueSigninToken(account.id, account.username));
											});
	}

private:
    AccountReadPort &accounts_;
    const PasswordVerifier &passwordVerifier_;
    const JwtIssuer &jwtIssuer_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::account
