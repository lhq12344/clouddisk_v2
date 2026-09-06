#pragma once

#include "../../domain/common/RequestContext.h"
#include "../../domain/common/Result.h"
#include "UploadStateMachine.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace core::application::upload
{

struct UploadSession
{
    std::uint64_t id{};
    std::string uploadId;
    std::uint64_t ownerAccountId{};
    std::string fileName;
    std::string fileHash;
    std::int64_t fileSize{};
    std::string contentType;
    std::string objectKey;
    std::string storageUploadId;
    std::int64_t partSize{};
    int totalParts{};
    UploadState state{UploadState::Initiated};
};

struct InitUploadCommand
{
    std::string fileName;
    std::string fileHash;
    std::int64_t fileSize{};
    std::string contentType;
};

struct InitUploadResult
{
    std::string uploadId;
    std::string objectKey;
    std::int64_t partSize{};
    int totalParts{};
    std::string status;
    std::string message;
};

struct PresignedPart
{
    int partNumber{};
    std::string url;
    std::string expiresAt;
};

struct PresignPartsCommand
{
    std::string uploadId;
    std::vector<int> partNumbers;
};

struct PresignPartsResult
{
    std::string uploadId;
    std::vector<PresignedPart> parts;
};

struct UploadStatusResult
{
    int totalParts{};
    std::vector<int> uploadedParts;
};

struct UploadedPart
{
    int partNumber{};
    std::string etag;
};

struct CompleteUploadResult
{
    std::string uploadId;
    std::string objectKey;
    std::string etag;
    std::string status;
};

class UploadSessionPort
{
public:
    virtual ~UploadSessionPort() = default;
    using SaveCallback = std::function<void(core::domain::Result<void>)>;
    using LoadCallback = std::function<void(core::domain::Result<UploadSession>)>;
    using StateCallback = std::function<void(core::domain::Result<void>)>;

    virtual void saveInitiatedSession(const UploadSession &session, SaveCallback callback) = 0;
    virtual void loadOwnedSession(const core::domain::RequestContext &ctx,
                                  const std::string &uploadId,
                                  LoadCallback callback) = 0;
    virtual void transitionState(const std::string &uploadId,
                                 UploadState expected,
                                 UploadState next,
                                 StateCallback callback) = 0;
};

class UploadFinalizationPort
{
public:
    virtual ~UploadFinalizationPort() = default;
    using FinalizeCallback = std::function<void(core::domain::Result<CompleteUploadResult>)>;
    virtual void finalizeCompletedUpload(const core::domain::RequestContext &ctx,
                                         const UploadSession &session,
                                         const std::string &objectEtag,
                                         FinalizeCallback callback) = 0;
};

class MultipartStorageControlPort
{
public:
    virtual ~MultipartStorageControlPort() = default;
    using InitCallback = std::function<void(core::domain::Result<UploadSession>)>;
    using PresignCallback = std::function<void(core::domain::Result<PresignPartsResult>)>;
    using StatusCallback = std::function<void(core::domain::Result<UploadStatusResult>)>;
    using ListPartsCallback = std::function<void(core::domain::Result<std::vector<UploadedPart>>)>;
    using CompleteCallback = std::function<void(core::domain::Result<CompleteUploadResult>)>;
    using AbortCallback = std::function<void(core::domain::Result<void>)>;

    virtual void initiateMultipart(const core::domain::RequestContext &ctx,
                                   const InitUploadCommand &command,
                                   InitCallback callback) = 0;
    virtual void presignParts(const core::domain::RequestContext &ctx,
                              const UploadSession &session,
                              const std::vector<int> &partNumbers,
                              PresignCallback callback) = 0;
    virtual void status(const core::domain::RequestContext &ctx,
                        const UploadSession &session,
                        StatusCallback callback) = 0;
    virtual void listParts(const core::domain::RequestContext &ctx,
                           const UploadSession &session,
                           ListPartsCallback callback) = 0;
    virtual void completeMultipart(const core::domain::RequestContext &ctx,
                                   const UploadSession &session,
                                   const std::vector<UploadedPart> &parts,
                                   CompleteCallback callback) = 0;
    virtual void abort(const core::domain::RequestContext &ctx,
                       const UploadSession &session,
                       AbortCallback callback) = 0;
};

} // namespace core::application::upload
