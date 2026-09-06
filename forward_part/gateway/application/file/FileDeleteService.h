#pragma once

#include "FilePorts.h"
#include "../common/FeatureFlags.h"

#include <utility>

namespace core::application::file
{

class FileDeleteService
{
public:
    using DeleteCallback = FileMutationPort::DeleteCallback;

    FileDeleteService(FileMutationPort &files, CoreFeatureFlags flags)
        : files_(files), flags_(flags) {}

    bool shouldUseCoreDeletePath() const
    {
        return flags_.fileDelete == ImplementationRoute::Core;
    }

    void deleteFile(const core::domain::RequestContext &ctx, DeleteFileCommand command, DeleteCallback callback)
    {
        if (!ctx.authenticated())
        {
            callback(core::domain::Result<DeleteFileResult>::fail(core::domain::ErrorCode::Unauthenticated,
                                                                  "missing authenticated request context"));
            return;
        }
        if (command.filename.empty())
        {
            callback(core::domain::Result<DeleteFileResult>::fail(core::domain::ErrorCode::InvalidArgument,
                                                                  "filename is required"));
            return;
        }
        files_.deleteOwnedFile(ctx, command, std::move(callback));
    }

private:
    FileMutationPort &files_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::file
