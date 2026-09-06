#pragma once

#include "../../application/account/AccountPorts.h"

namespace core::infrastructure::jwt
{

class LegacyJwtIssuer final : public core::application::account::JwtIssuer
{
public:
    explicit LegacyJwtIssuer(std::string signingKey) : signingKey_(std::move(signingKey)) {}

    core::domain::Result<std::string> issueSigninToken(std::uint64_t userId, const std::string &username) const override;

private:
    std::string signingKey_;
};

} // namespace core::infrastructure::jwt
