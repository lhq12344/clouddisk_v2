#pragma once

#include "../../application/common/TransactionRunner.h"

namespace core::infrastructure::mysql
{

class MysqlTransactionContext final : public core::application::TransactionContext
{
};

class MysqlTransactionRunner final : public core::application::TransactionRunner
{
public:
    core::domain::Result<void> run(const std::string &operationName,
                                   const std::function<core::domain::Result<void>(core::application::TransactionContext &)> &operation) override;
};

} // namespace core::infrastructure::mysql
