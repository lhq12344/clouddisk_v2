#pragma once

#include "../../application/upload/UploadPorts.h"

#include <drogon/orm/DbClient.h>

#include <utility>

namespace core::infrastructure::mysql
{

class MysqlUploadSessionRepository final : public core::application::upload::UploadSessionPort,
                                          public core::application::upload::UploadFinalizationPort
{
public:
    explicit MysqlUploadSessionRepository(drogon::orm::DbClientPtr dbClient) : dbClient_(std::move(dbClient)) {}

    void saveInitiatedSession(const core::application::upload::UploadSession &session, SaveCallback callback) override;
    void loadOwnedSession(const core::domain::RequestContext &ctx,
                          const std::string &uploadId,
                          LoadCallback callback) override;
    void transitionState(const std::string &uploadId,
                         core::application::upload::UploadState expected,
                         core::application::upload::UploadState next,
                         StateCallback callback) override;
    void finalizeCompletedUpload(const core::domain::RequestContext &ctx,
                                 const core::application::upload::UploadSession &session,
                                 const std::string &objectEtag,
                                 FinalizeCallback callback) override;

private:
    drogon::orm::DbClientPtr dbClient_;
};

} // namespace core::infrastructure::mysql
