#include "MysqlTransactionRunner.h"

namespace core::infrastructure::mysql
{

core::domain::Result<void> MysqlTransactionRunner::run(
    const std::string &operationName,
    const std::function<core::domain::Result<void>(core::application::TransactionContext &)> &operation)
{
    (void)operationName;
    MysqlTransactionContext context;
    return operation(context);
}

} // namespace core::infrastructure::mysql
