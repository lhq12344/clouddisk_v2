#pragma once

#include "../../application/file/FilePorts.h"

#include <drogon/orm/DbClient.h>

#include <utility>

namespace core::infrastructure::mysql
{

class MysqlFileRepository final : public core::application::file::FileReadPort,
                                  public core::application::file::FileMutationPort
{
public:
    explicit MysqlFileRepository(drogon::orm::DbClientPtr dbClient) : dbClient_(std::move(dbClient)) {}

    void listOwnedFiles(const core::domain::RequestContext &ctx, ListCallback callback) override;
    void resolveOwnedFile(const core::domain::RequestContext &ctx,
                          const core::application::file::FileAccessRequest &request,
                          ResolveCallback callback) override;
    void deleteOwnedFile(const core::domain::RequestContext &ctx,
                         const core::application::file::DeleteFileCommand &command,
                         DeleteCallback callback) override;

private:
    drogon::orm::DbClientPtr dbClient_;
};

} // namespace core::infrastructure::mysql
