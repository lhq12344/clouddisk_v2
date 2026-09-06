#pragma once

#include "../../application/account/AccountPorts.h"

namespace core::infrastructure::jwt
{

class LegacyPasswordVerifier final : public core::application::account::PasswordVerifier,
                                     public core::application::account::PasswordHasher
{
public:
    bool verifyLegacyPassword(const std::string &rawPassword,
                              const std::string &salt,
                              const std::string &encodedPassword) const override;

    core::domain::Result<core::application::account::PendingRegistration> hashPendingRegistration(
        const std::string &username,
        const std::string &email,
        const std::string &rawPassword) const override;
};

} // namespace core::infrastructure::jwt
