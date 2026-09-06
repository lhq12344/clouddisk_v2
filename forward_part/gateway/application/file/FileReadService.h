#pragma once

#include "FilePorts.h"
#include "../common/FeatureFlags.h"

#include <memory>
#include <utility>

namespace core::application::file
{

class FileReadService
{
public:
    using ListCallback = FileReadPort::ListCallback;
    using AccessCallback = std::function<void(core::domain::Result<std::string>)>;

    FileReadService(FileReadPort &files,
                    StorageControlPort &storageControl,
                    FileAuthorizationPolicy authorizationPolicy,
                    CoreFeatureFlags flags)
        : files_(files), storageControl_(storageControl), authorizationPolicy_(authorizationPolicy), flags_(flags) {}

    bool shouldUseCoreListPath() const
    {
        return flags_.fileReads == ImplementationRoute::Core || flags_.fileReads == ImplementationRoute::Shadow;
    }

    bool shouldUseCoreAccessPath() const
    {
        return flags_.fileAccess == ImplementationRoute::Core || flags_.fileAccess == ImplementationRoute::Shadow;
    }

    void listFiles(const core::domain::RequestContext &ctx, ListCallback callback)
    {
        if (!ctx.authenticated())
        {
            callback(core::domain::Result<std::vector<FileListItem>>::fail(core::domain::ErrorCode::Unauthenticated,
                                                                           "missing authenticated request context"));
            return;
        }
        files_.listOwnedFiles(ctx, std::move(callback));
    }

    void presignDownload(const core::domain::RequestContext &ctx,
                         FileAccessRequest request,
                         bool inlineDisposition,
                         AccessCallback callback)
    {
        if (!ctx.authenticated())
        {
            callback(core::domain::Result<std::string>::fail(core::domain::ErrorCode::Unauthenticated,
                                                             "missing authenticated request context"));
            return;
        }
        auto requestPtr = std::make_shared<FileAccessRequest>(std::move(request));
        files_.resolveOwnedFile(ctx, *requestPtr,
                                [this, ctx, requestPtr, inlineDisposition, callback = std::move(callback)](auto fileResult) mutable {
                                    if (!fileResult.ok())
                                    {
                                        callback(core::domain::Result<std::string>::fail(fileResult.error.code, fileResult.error.message));
                                        return;
                                    }
                                    auto file = std::make_shared<FileListItem>(std::move(fileResult.value));
                                    if (!authorizationPolicy_.canReadObject(ctx, file->ownerId, file->status))
                                    {
                                        callback(core::domain::Result<std::string>::fail(core::domain::ErrorCode::Conflict,
                                                                                         unavailableMessage(file->status)));
                                        return;
                                    }

                                    PresignedGetRequest presignRequest;
                                    presignRequest.objectKey = file->objectKey.empty() ? "files/" + file->filehash : file->objectKey;
                                    presignRequest.filename = file->filename;
                                    presignRequest.contentType = file->contentType;
                                    presignRequest.inlineDisposition = inlineDisposition;
                                    storageControl_.presignGet(ctx, *file, presignRequest,
                                                               [file, callback = std::move(callback)](auto urlResult) mutable {
                                                                   callback(std::move(urlResult));
                                                               });
                                });
    }

private:
    static std::string unavailableMessage(const std::string &status)
    {
        if (status == "pending_scan")
        {
            return "file is pending virus scan";
        }
        if (status == "infected")
        {
            return "file is blocked by virus scan";
        }
        if (status == "scan_failed")
        {
            return "file virus scan failed";
        }
        return "file is unavailable";
    }

    FileReadPort &files_;
    StorageControlPort &storageControl_;
    FileAuthorizationPolicy authorizationPolicy_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::file
