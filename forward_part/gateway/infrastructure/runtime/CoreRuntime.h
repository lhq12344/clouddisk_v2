#pragma once

#include "../../application/common/FeatureFlags.h"
#include "../../../internal/internal.h"

#include <mutex>
#include <string>
#include <vector>

namespace core::infrastructure::runtime
{

struct DependencyStatus
{
    std::string name;
    bool configured{false};
    bool ready{false};
    std::string detail;
};

struct RuntimeSnapshot
{
    bool initialized{false};
    bool ready{false};
    std::string configSource;
    std::string configVersion;
    std::vector<DependencyStatus> dependencies;
    core::application::CoreFeatureFlags flags;
};

class CoreRuntime
{
public:
    static CoreRuntime &instance();

    void initialize(const AppConfig &config, std::string configSource, std::string configVersion);
    RuntimeSnapshot snapshot() const;
    bool ready() const;

private:
    CoreRuntime() = default;

    mutable std::mutex mutex_;
    RuntimeSnapshot snapshot_;
};

bool hasText(const std::string &value);
bool validPort(const std::string &value);

} // namespace core::infrastructure::runtime
