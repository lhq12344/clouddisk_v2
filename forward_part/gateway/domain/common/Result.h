#pragma once

#include <string>
#include <utility>

namespace core::domain
{

enum class ErrorCode
{
    None,
    InvalidArgument,
    Unauthenticated,
    PermissionDenied,
    NotFound,
    Conflict,
    FailedPrecondition,
    DependencyUnavailable,
    Internal
};

struct Error
{
    ErrorCode code{ErrorCode::None};
    std::string message;

    bool ok() const
    {
        return code == ErrorCode::None;
    }
};

template <typename T>
struct Result
{
    T value{};
    Error error{};

    static Result<T> ok(T v)
    {
        Result<T> result;
        result.value = std::move(v);
        return result;
    }

    static Result<T> fail(ErrorCode code, std::string message)
    {
        Result<T> result;
        result.error = Error{code, std::move(message)};
        return result;
    }

    bool ok() const
    {
        return error.ok();
    }
};

template <>
struct Result<void>
{
    Error error{};

    static Result<void> success()
    {
        return Result<void>{};
    }

    static Result<void> fail(ErrorCode code, std::string message)
    {
        Result<void> result;
        result.error = Error{code, std::move(message)};
        return result;
    }

    bool ok() const
    {
        return error.ok();
    }
};

} // namespace core::domain
