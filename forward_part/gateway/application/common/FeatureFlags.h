#pragma once

#include <string>

namespace core::application
{

enum class ImplementationRoute
{
    Legacy,
    Core,
    Shadow
};

struct CoreFeatureFlags
{
    ImplementationRoute accountReads{ImplementationRoute::Core};
    ImplementationRoute accountLogin{ImplementationRoute::Core};
    ImplementationRoute accountRegistration{ImplementationRoute::Core};
    ImplementationRoute fileReads{ImplementationRoute::Core};
    ImplementationRoute fileAccess{ImplementationRoute::Core};
    ImplementationRoute uploadControl{ImplementationRoute::Core};
    ImplementationRoute uploadComplete{ImplementationRoute::Core};
    ImplementationRoute fileDelete{ImplementationRoute::Core};
    ImplementationRoute smallUploadDirect{ImplementationRoute::Core};
    ImplementationRoute outboxRelay{ImplementationRoute::Core};

    static CoreFeatureFlags fromEnvironment();
};

ImplementationRoute parseImplementationRoute(const char *raw, ImplementationRoute fallback = ImplementationRoute::Core);
std::string toString(ImplementationRoute route);

} // namespace core::application
