#pragma once

#include "../../domain/common/Result.h"

#include <functional>
#include <string>

namespace core::application
{

class TransactionContext
{
public:
    virtual ~TransactionContext() = default;
};

class TransactionRunner
{
public:
    virtual ~TransactionRunner() = default;

    virtual core::domain::Result<void> run(const std::string &operationName,
                                           const std::function<core::domain::Result<void>(TransactionContext &)> &operation) = 0;
};

} // namespace core::application
