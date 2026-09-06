#include "LegacyJwtIssuer.h"

#include <chrono>
#include <jwt-cpp/jwt.h>

namespace core::infrastructure::jwt
{

core::domain::Result<std::string> LegacyJwtIssuer::issueSigninToken(std::uint64_t userId, const std::string &username) const
{
    if (signingKey_.empty())
    {
        return core::domain::Result<std::string>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                       "jwt signing key is not configured");
    }

    try
    {
        const auto now = std::chrono::system_clock::now();
        auto token = jwt::create()
                         .set_issuer("Signin")
                         .set_issued_at(now)
                         .set_expires_at(now + std::chrono::hours(24))
                         .set_payload_claim("ID", jwt::claim(static_cast<int64_t>(userId)))
                         .set_payload_claim("Name", jwt::claim(username))
                         .sign(jwt::algorithm::hs256{signingKey_});
        return core::domain::Result<std::string>::ok(std::move(token));
    }
    catch (const std::exception &e)
    {
        return core::domain::Result<std::string>::fail(core::domain::ErrorCode::Internal, e.what());
    }
}

} // namespace core::infrastructure::jwt
