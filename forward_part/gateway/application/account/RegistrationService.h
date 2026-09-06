#pragma once

#include "AccountPorts.h"
#include "../common/FeatureFlags.h"

#include <memory>
#include <utility>

namespace core::application::account
{

struct SignupCommand
{
    std::string username;
    std::string password;
    std::string email;
};

struct VerifyCodeCommand
{
    std::string email;
    std::string code;
};

class RegistrationService
{
public:
    using VoidCallback = std::function<void(core::domain::Result<void>)>;

    RegistrationService(AccountReadPort &accounts,
                        AccountWritePort &accountWriter,
                        PendingRegistrationPort &pendingRegistrations,
                        const PasswordHasher &passwordHasher,
                        CoreFeatureFlags flags)
        : accounts_(accounts), accountWriter_(accountWriter), pendingRegistrations_(pendingRegistrations), passwordHasher_(passwordHasher), flags_(flags) {}

    bool shouldUseCoreRegistrationPath() const
    {
        return flags_.accountRegistration == ImplementationRoute::Core;
    }

    void signup(SignupCommand command, VoidCallback callback)
    {
        if (command.username.empty() || command.password.empty() || command.email.empty())
        {
            callback(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                      "username, password and email are required"));
            return;
        }

        auto commandPtr = std::make_shared<SignupCommand>(std::move(command));
        accounts_.accountExistsByUsername(commandPtr->username,
                                          [this, commandPtr, callback = std::move(callback)](auto usernameExists) mutable {
                                              if (!usernameExists.ok())
                                              {
                                                  callback(core::domain::Result<void>::fail(usernameExists.error.code, usernameExists.error.message));
                                                  return;
                                              }
                                              if (usernameExists.value)
                                              {
                                                  callback(core::domain::Result<void>::fail(core::domain::ErrorCode::Conflict, "account exists"));
                                                  return;
                                              }
                                              accounts_.accountExistsByEmail(commandPtr->email,
                                                                            [this, commandPtr, callback = std::move(callback)](auto emailExists) mutable {
                                                                                if (!emailExists.ok())
                                                                                {
                                                                                    callback(core::domain::Result<void>::fail(emailExists.error.code, emailExists.error.message));
                                                                                    return;
                                                                                }
                                                                                if (emailExists.value)
                                                                                {
                                                                                    callback(core::domain::Result<void>::fail(core::domain::ErrorCode::Conflict, "email already registered"));
                                                                                    return;
                                                                                }

                                                                                auto pending = passwordHasher_.hashPendingRegistration(commandPtr->username, commandPtr->email, commandPtr->password);
                                                                                if (!pending.ok())
                                                                                {
                                                                                    callback(core::domain::Result<void>::fail(pending.error.code, pending.error.message));
                                                                                    return;
                                                                                }
                                                                                pendingRegistrations_.storePendingRegistration(pending.value, std::move(callback));
                                                                            });
                                          });
    }

    void verifyCode(VerifyCodeCommand command, VoidCallback callback)
    {
        if (command.email.empty() || command.code.empty())
        {
            callback(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                      "email and code are required"));
            return;
        }

        auto commandPtr = std::make_shared<VerifyCodeCommand>(std::move(command));
        pendingRegistrations_.loadVerificationCode(commandPtr->email,
                                                   [this, commandPtr, callback = std::move(callback)](auto codeResult) mutable {
                                                       if (!codeResult.ok())
                                                       {
                                                           callback(core::domain::Result<void>::fail(codeResult.error.code, codeResult.error.message));
                                                           return;
                                                       }
                                                       if (codeResult.value != commandPtr->code)
                                                       {
                                                           callback(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                                                                      "Verification code does not match"));
                                                           return;
                                                       }

                                                       pendingRegistrations_.loadPendingRegistration(commandPtr->email,
                                                                                                      [this, commandPtr, callback = std::move(callback)](auto pendingResult) mutable {
                                                                                                          if (!pendingResult.ok())
                                                                                                          {
                                                                                                              accounts_.accountExistsByEmail(commandPtr->email,
                                                                                                                                            [callback = std::move(callback), error = pendingResult.error](auto exists) mutable {
                                                                                                                                                if (exists.ok() && exists.value)
                                                                                                                                                {
                                                                                                                                                    callback(core::domain::Result<void>::success());
                                                                                                                                                    return;
                                                                                                                                                }
                                                                                                                                                callback(core::domain::Result<void>::fail(error.code, error.message));
                                                                                                                                            });
                                                                                                              return;
                                                                                                          }

                                                                                                          auto pending = std::make_shared<PendingRegistration>(std::move(pendingResult.value));
                                                                                                          accountWriter_.createAccountFromPendingRegistration(*pending,
                                                                                                                                                              [this, pending, callback = std::move(callback)](auto createResult) mutable {
                                                                                                                                                                  if (!createResult.ok() && createResult.error.code != core::domain::ErrorCode::Conflict)
                                                                                                                                                                  {
                                                                                                                                                                      callback(core::domain::Result<void>::fail(createResult.error.code, createResult.error.message));
                                                                                                                                                                      return;
                                                                                                                                                                  }
                                                                                                                                                                  pendingRegistrations_.deletePendingRegistrationAndCode(pending->email, std::move(callback));
                                                                                                                                                              });
                                                                                                      });
                                                   });
    }

private:
    AccountReadPort &accounts_;
    AccountWritePort &accountWriter_;
    PendingRegistrationPort &pendingRegistrations_;
    const PasswordHasher &passwordHasher_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::account
