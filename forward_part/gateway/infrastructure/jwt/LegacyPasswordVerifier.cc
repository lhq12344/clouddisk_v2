#include "LegacyPasswordVerifier.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace core::infrastructure::jwt
{
namespace
{
constexpr int kLegacyIterations = 100;
constexpr int kLegacyKeyLen = 32;
constexpr int kLegacySaltLen = 16;
constexpr char kSaltAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

std::string toHex(const std::vector<unsigned char> &bytes)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : bytes)
    {
        out << std::setw(2) << static_cast<int>(byte);
    }
    return out.str();
}

core::domain::Result<std::string> generateLegacySalt()
{
    std::vector<unsigned char> bytes(kLegacySaltLen);
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        return core::domain::Result<std::string>::fail(core::domain::ErrorCode::Internal,
                                                       "failed to generate registration salt");
    }

    std::string salt;
    salt.reserve(kLegacySaltLen);
    constexpr auto alphabetLen = sizeof(kSaltAlphabet) - 1;
    for (const auto byte : bytes)
    {
        salt.push_back(kSaltAlphabet[byte % alphabetLen]);
    }
    return core::domain::Result<std::string>::ok(std::move(salt));
}

core::domain::Result<std::string> legacyEncodePassword(const std::string &rawPassword, const std::string &salt)
{
    std::vector<unsigned char> derived(kLegacyKeyLen);
    const auto ok = PKCS5_PBKDF2_HMAC(rawPassword.c_str(), static_cast<int>(rawPassword.size()),
                                      reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                                      kLegacyIterations, EVP_md5(), kLegacyKeyLen, derived.data());
    if (ok != 1)
    {
        return core::domain::Result<std::string>::fail(core::domain::ErrorCode::Internal,
                                                       "failed to encode legacy password");
    }
    return core::domain::Result<std::string>::ok(toHex(derived));
}
} // namespace

bool LegacyPasswordVerifier::verifyLegacyPassword(const std::string &rawPassword,
                                                  const std::string &salt,
                                                  const std::string &encodedPassword) const
{
    std::vector<unsigned char> derived(kLegacyKeyLen);
    const auto ok = PKCS5_PBKDF2_HMAC(rawPassword.c_str(), static_cast<int>(rawPassword.size()),
                                      reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                                      kLegacyIterations, EVP_md5(), kLegacyKeyLen, derived.data());
    return ok == 1 && toHex(derived) == encodedPassword;
}

core::domain::Result<core::application::account::PendingRegistration> LegacyPasswordVerifier::hashPendingRegistration(
    const std::string &username,
    const std::string &email,
    const std::string &rawPassword) const
{
    auto salt = generateLegacySalt();
    if (!salt.ok())
    {
        return core::domain::Result<core::application::account::PendingRegistration>::fail(salt.error.code, salt.error.message);
    }
    auto encoded = legacyEncodePassword(rawPassword, salt.value);
    if (!encoded.ok())
    {
        return core::domain::Result<core::application::account::PendingRegistration>::fail(encoded.error.code, encoded.error.message);
    }

    core::application::account::PendingRegistration pending;
    pending.username = username;
    pending.email = email;
    pending.salt = std::move(salt.value);
    pending.passwordHash = std::move(encoded.value);
    return core::domain::Result<core::application::account::PendingRegistration>::ok(std::move(pending));
}

} // namespace core::infrastructure::jwt
