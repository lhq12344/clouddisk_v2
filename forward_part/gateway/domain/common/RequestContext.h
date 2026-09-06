#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace core::domain
{

struct RequestContext
{
    std::string requestId;
    std::optional<std::uint64_t> userId;
    std::string username;

    bool authenticated() const
    {
        return userId.has_value() && !username.empty();
    }
};

} // namespace core::domain
