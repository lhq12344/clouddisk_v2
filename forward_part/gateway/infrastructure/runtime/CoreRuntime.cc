#include "CoreRuntime.h"

#include <cstdlib>
#include <utility>

namespace core::infrastructure::runtime
{

namespace
{
DependencyStatus configuredDependency(std::string name, bool configured, std::string detail)
{
    return DependencyStatus{std::move(name), configured, configured, std::move(detail)};
}
} // namespace

CoreRuntime &CoreRuntime::instance()
{
    static CoreRuntime runtime;
    return runtime;
}

bool hasText(const std::string &value)
{
    return !value.empty();
}

bool validPort(const std::string &value)
{
    if (value.empty())
    {
        return false;
    }
    char *end = nullptr;
    const auto port = std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0' && port > 0 && port <= 65535;
}

void CoreRuntime::initialize(const AppConfig &config, std::string configSource, std::string configVersion)
{
    RuntimeSnapshot next;
    next.initialized = true;
    next.configSource = std::move(configSource);
    next.configVersion = std::move(configVersion);
    next.flags = core::application::CoreFeatureFlags::fromEnvironment();

    next.dependencies.push_back(configuredDependency(
        "mysql",
        hasText(config.mysql.host) && validPort(config.mysql.port) && hasText(config.mysql.user) && hasText(config.mysql.password),
        config.mysql.host + ":" + config.mysql.port));
    next.dependencies.push_back(configuredDependency(
        "redis",
        hasText(config.redis.host) && validPort(config.redis.port),
        config.redis.host + ":" + config.redis.port));
    next.dependencies.push_back(configuredDependency(
        "consul",
        hasText(config.consul.host) && validPort(config.consul.port),
        config.consul.host + ":" + config.consul.port));
    next.dependencies.push_back(configuredDependency(
        "storage_control",
        hasText(config.consul.storage_control.host) && validPort(config.consul.storage_control.port),
        config.consul.storage_control.host + ":" + config.consul.storage_control.port));
    next.dependencies.push_back(configuredDependency(
        "kafka",
        hasText(config.kafka.host) && validPort(config.kafka.port),
        config.kafka.host + ":" + config.kafka.port));
    next.dependencies.push_back(configuredDependency(
        "jwt",
        hasText(config.jwt.secret),
        hasText(config.jwt.secret) ? "signing key configured" : "missing signing key"));

    next.ready = true;
    for (const auto &dependency : next.dependencies)
    {
        if (!dependency.configured || !dependency.ready)
        {
            next.ready = false;
            break;
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = std::move(next);
}

RuntimeSnapshot CoreRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

bool CoreRuntime::ready() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_.initialized && snapshot_.ready;
}

} // namespace core::infrastructure::runtime
