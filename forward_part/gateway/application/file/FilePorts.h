#pragma once

#include "../../domain/common/RequestContext.h"
#include "../../domain/common/Result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace core::application::file
{

struct FileListItem
{
    std::uint64_t fileId{};
    std::uint64_t ownerId{};
    std::string filename;
    std::string filehash;
    std::int64_t filesize{};
    std::string status;
    std::string scanDetail;
    std::string contentType;
    std::string objectKey;
    std::string createdAt;
    std::string updatedAt;
};

struct FileAccessRequest
{
    std::string filename;
    std::string filehash;
    std::int64_t fileSize{};
};

struct PresignedGetRequest
{
    std::string objectKey;
    std::string filename;
    std::string contentType;
    bool inlineDisposition{false};
};

struct DeleteFileCommand
{
    std::string filename;
    std::string filehash;
};

struct DeleteFileResult
{
    bool objectDeleteQueued{false};
    std::uint64_t fileId{};
    std::string objectKey;
};

class FileReadPort
{
public:
    virtual ~FileReadPort() = default;
    using ListCallback = std::function<void(core::domain::Result<std::vector<FileListItem>>)>;
    using ResolveCallback = std::function<void(core::domain::Result<FileListItem>)>;

    virtual void listOwnedFiles(const core::domain::RequestContext &ctx, ListCallback callback) = 0;
    virtual void resolveOwnedFile(const core::domain::RequestContext &ctx,
                                  const FileAccessRequest &request,
                                  ResolveCallback callback) = 0;
};

class FileMutationPort
{
public:
    virtual ~FileMutationPort() = default;
    using DeleteCallback = std::function<void(core::domain::Result<DeleteFileResult>)>;
    virtual void deleteOwnedFile(const core::domain::RequestContext &ctx,
                                 const DeleteFileCommand &command,
                                 DeleteCallback callback) = 0;
};

class StorageControlPort
{
public:
    virtual ~StorageControlPort() = default;
    using PresignCallback = std::function<void(core::domain::Result<std::string>)>;
    virtual void presignGet(const core::domain::RequestContext &ctx,
                            const FileListItem &file,
                            const PresignedGetRequest &request,
                            PresignCallback callback) = 0;
};

class FileAuthorizationPolicy
{
public:
    bool canReadObject(const core::domain::RequestContext &ctx, std::uint64_t ownerId, const std::string &scanStatus) const
    {
        return ctx.userId.has_value() && ctx.userId.value() == ownerId && scanStatus == "success";
    }
};

} // namespace core::application::file
