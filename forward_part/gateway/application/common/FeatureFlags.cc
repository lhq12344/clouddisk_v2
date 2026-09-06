#include "FeatureFlags.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace core::application
{
namespace
{
std::string normalize(const char *raw)
{
    if (raw == nullptr)
    {
        return "";
    }
    std::string value(raw);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string environmentValue(const char *key)
{
#ifdef _WIN32
    char *buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, key) != 0 || buffer == nullptr)
    {
        return "";
    }
    std::string value(buffer);
    std::free(buffer);
    return value;
#else
    const char *value = std::getenv(key);
    return value == nullptr ? "" : std::string(value);
#endif
}
} // namespace

ImplementationRoute parseImplementationRoute(const char *raw, ImplementationRoute fallback)
{
    const auto value = normalize(raw);
    if (value == "core" || value == "new")
    {
        return ImplementationRoute::Core;
    }
    if (value == "shadow" || value == "compare")
    {
        return ImplementationRoute::Shadow;
    }
    if (value == "legacy" || value == "old")
    {
        return ImplementationRoute::Legacy;
    }
    return fallback;
}

std::string toString(ImplementationRoute route)
{
    switch (route)
    {
    case ImplementationRoute::Core:
        return "core";
    case ImplementationRoute::Shadow:
        return "shadow";
    case ImplementationRoute::Legacy:
    default:
        return "legacy";
    }
}

CoreFeatureFlags CoreFeatureFlags::fromEnvironment()
{
    const auto routeFromEnv = [](const char *key) {
        const auto value = environmentValue(key);
        return parseImplementationRoute(value.c_str());
    };

    CoreFeatureFlags flags;
    flags.accountReads = routeFromEnv("CORE_ACCOUNT_READS");
    flags.accountLogin = routeFromEnv("CORE_ACCOUNT_LOGIN");
    flags.accountRegistration = routeFromEnv("CORE_ACCOUNT_REGISTRATION");
    flags.fileReads = routeFromEnv("CORE_FILE_READS");
    flags.fileAccess = routeFromEnv("CORE_FILE_ACCESS");
    flags.uploadControl = routeFromEnv("CORE_UPLOAD_CONTROL");
    flags.uploadComplete = routeFromEnv("CORE_UPLOAD_COMPLETE");
    flags.fileDelete = routeFromEnv("CORE_FILE_DELETE");
    flags.smallUploadDirect = routeFromEnv("CORE_SMALL_UPLOAD_DIRECT");
    flags.outboxRelay = routeFromEnv("CORE_OUTBOX_RELAY");
    return flags;
}

} // namespace core::application
