#pragma once

#include "AccountPorts.h"
#include "../common/FeatureFlags.h"

#include <utility>

namespace core::application::account
{

class UserInfoService
{
public:
    UserInfoService(AccountReadPort &coreReadPort, CoreFeatureFlags flags)
        : coreReadPort_(coreReadPort), flags_(flags) {}

    bool shouldUseCoreReadPath() const
    {
        return flags_.accountReads == ImplementationRoute::Core || flags_.accountReads == ImplementationRoute::Shadow;
    }

    void getUserInfo(const core::domain::RequestContext &ctx, AccountReadPort::UserInfoCallback callback)
    {
        coreReadPort_.getUserInfo(ctx, std::move(callback));
    }

private:
    AccountReadPort &coreReadPort_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::account
