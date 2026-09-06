#pragma once

#include "../../application/file/FilePorts.h"
#include "../../application/upload/UploadPorts.h"
#include "../../../../proto/storage_control/storage_control.grpc.pb.h"

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace core::infrastructure::storage_control
{

class GrpcStorageControlAdapter final : public core::application::file::StorageControlPort,
                                       public core::application::upload::MultipartStorageControlPort
{
public:
    explicit GrpcStorageControlAdapter(std::shared_ptr<::storage_control::StorageControl::Stub> stub)
        : stub_(std::move(stub)) {}

    void presignGet(const core::domain::RequestContext &ctx,
                    const core::application::file::FileListItem &file,
                    const core::application::file::PresignedGetRequest &request,
                    PresignCallback callback) override;

    void initiateMultipart(const core::domain::RequestContext &ctx,
                           const core::application::upload::InitUploadCommand &command,
                           InitCallback callback) override;
    void presignParts(const core::domain::RequestContext &ctx,
                      const core::application::upload::UploadSession &session,
                      const std::vector<int> &partNumbers,
                      PresignCallback callback) override;
    void status(const core::domain::RequestContext &ctx,
                const core::application::upload::UploadSession &session,
                StatusCallback callback) override;
    void listParts(const core::domain::RequestContext &ctx,
                   const core::application::upload::UploadSession &session,
                   ListPartsCallback callback) override;
    void completeMultipart(const core::domain::RequestContext &ctx,
                           const core::application::upload::UploadSession &session,
                           const std::vector<core::application::upload::UploadedPart> &parts,
                           CompleteCallback callback) override;
    void abort(const core::domain::RequestContext &ctx,
               const core::application::upload::UploadSession &session,
               AbortCallback callback) override;

private:
    std::shared_ptr<::storage_control::StorageControl::Stub> stub_;
};

} // namespace core::infrastructure::storage_control
