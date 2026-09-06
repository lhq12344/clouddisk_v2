#include "GrpcStorageControlAdapter.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <random>
#include <sstream>
#include <thread>
#include <utility>

namespace core::infrastructure::storage_control
{
namespace
{
constexpr std::int64_t kDefaultPartSize = 10LL * 1024LL * 1024LL;
constexpr int kMaxMultipartParts = 10000;
constexpr std::int64_t kPartPresignExpirySeconds = 15 * 60;
constexpr std::int64_t kGetPresignExpirySeconds = 10 * 60;
constexpr int kDefaultRpcDeadlineSeconds = 15;
constexpr int kCompleteRpcDeadlineSeconds = 60;

core::domain::ErrorCode mapGrpcError(const ::grpc::Status &status)
{
    switch (status.error_code())
    {
    case ::grpc::StatusCode::INVALID_ARGUMENT:
        return core::domain::ErrorCode::InvalidArgument;
    case ::grpc::StatusCode::NOT_FOUND:
        return core::domain::ErrorCode::NotFound;
    case ::grpc::StatusCode::PERMISSION_DENIED:
        return core::domain::ErrorCode::PermissionDenied;
    case ::grpc::StatusCode::FAILED_PRECONDITION:
        return core::domain::ErrorCode::FailedPrecondition;
    case ::grpc::StatusCode::ABORTED:
    case ::grpc::StatusCode::ALREADY_EXISTS:
        return core::domain::ErrorCode::Conflict;
    case ::grpc::StatusCode::UNAVAILABLE:
    case ::grpc::StatusCode::DEADLINE_EXCEEDED:
        return core::domain::ErrorCode::DependencyUnavailable;
    default:
        return core::domain::ErrorCode::Internal;
    }
}

std::string grpcErrorMessage(const ::grpc::Status &status)
{
    if (!status.error_message().empty())
    {
        return status.error_message();
    }
    return "storage_control grpc call failed";
}

void addCommonMetadata(::grpc::ClientContext &grpcContext, const core::domain::RequestContext &ctx, int deadlineSeconds)
{
    if (!ctx.requestId.empty())
    {
        grpcContext.AddMetadata("x-request-id", ctx.requestId);
    }
    grpcContext.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(deadlineSeconds));
}

std::string trim(std::string value)
{
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string buildObjectKey(const std::string &fileHash)
{
    return "files/" + trim(fileHash);
}

std::string generateUploadId()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << dist(gen)
        << std::setw(16) << dist(gen);
    return out.str();
}

std::int64_t choosePartSize(std::int64_t fileSize)
{
    if (fileSize <= 0)
    {
        return kDefaultPartSize;
    }
    std::int64_t partSize = fileSize < kDefaultPartSize ? fileSize : kDefaultPartSize;
    while ((fileSize + partSize - 1) / partSize > kMaxMultipartParts)
    {
        partSize *= 2;
    }
    return partSize;
}

int totalParts(std::int64_t fileSize, std::int64_t partSize)
{
    if (fileSize <= 0 || partSize <= 0)
    {
        return 0;
    }
    return static_cast<int>((fileSize + partSize - 1) / partSize);
}

template <typename T>
bool requireStub(const std::shared_ptr<::storage_control::StorageControl::Stub> &stub,
                 std::function<void(core::domain::Result<T>)> &callback)
{
    if (stub)
    {
        return true;
    }
    callback(core::domain::Result<T>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                           "storage_control client unavailable"));
    return false;
}

bool requireStub(const std::shared_ptr<::storage_control::StorageControl::Stub> &stub,
                 std::function<void(core::domain::Result<void>)> &callback)
{
    if (stub)
    {
        return true;
    }
    callback(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                             "storage_control client unavailable"));
    return false;
}
} // namespace

void GrpcStorageControlAdapter::presignGet(const core::domain::RequestContext &ctx,
                                           const core::application::file::FileListItem &file,
                                           const core::application::file::PresignedGetRequest &request,
                                           PresignCallback callback)
{
    if (!requireStub<std::string>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<PresignCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, file, request, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
        ::storage_control::PresignGetReq grpcRequest;
        ::storage_control::PresignGetResp grpcResponse;
        grpcRequest.set_object_key(request.objectKey.empty() ? file.objectKey : request.objectKey);
        grpcRequest.set_filename(request.filename.empty() ? file.filename : request.filename);
        grpcRequest.set_content_type(request.contentType.empty() ? file.contentType : request.contentType);
        grpcRequest.set_inline_disposition(request.inlineDisposition);
        grpcRequest.set_expires_seconds(kGetPresignExpirySeconds);
        const auto status = stub->PresignGet(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<std::string>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        (*callbackPtr)(core::domain::Result<std::string>::ok(grpcResponse.url()));
    }).detach();
}

void GrpcStorageControlAdapter::initiateMultipart(const core::domain::RequestContext &ctx,
                                                  const core::application::upload::InitUploadCommand &command,
                                                  InitCallback callback)
{
    if (!requireStub<core::application::upload::UploadSession>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<InitCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, command, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
        ::storage_control::InitiateMultipartReq grpcRequest;
        ::storage_control::InitiateMultipartResp grpcResponse;
        grpcRequest.set_object_key(buildObjectKey(command.fileHash));
        grpcRequest.set_content_type(command.contentType.empty() ? "application/octet-stream" : command.contentType);
        (*grpcRequest.mutable_metadata())["x-file-name"] = command.fileName;
        (*grpcRequest.mutable_metadata())["x-file-hash"] = command.fileHash;
        if (ctx.userId.has_value())
        {
            (*grpcRequest.mutable_metadata())["x-user-id"] = std::to_string(ctx.userId.value());
        }

        const auto status = stub->InitiateMultipart(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        const auto partSize = choosePartSize(command.fileSize);
        core::application::upload::UploadSession session;
        session.uploadId = generateUploadId();
        session.ownerAccountId = ctx.userId.value_or(0);
        session.fileName = command.fileName;
        session.fileHash = command.fileHash;
        session.fileSize = command.fileSize;
        session.contentType = command.contentType;
        session.objectKey = grpcRequest.object_key();
        session.storageUploadId = grpcResponse.storage_upload_id();
        session.partSize = partSize;
        session.totalParts = totalParts(command.fileSize, partSize);
        session.state = core::application::upload::UploadState::Initiated;
        (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::ok(std::move(session)));
    }).detach();
}

void GrpcStorageControlAdapter::presignParts(const core::domain::RequestContext &ctx,
                                             const core::application::upload::UploadSession &session,
                                             const std::vector<int> &partNumbers,
                                             PresignCallback callback)
{
    if (!requireStub<core::application::upload::PresignPartsResult>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<PresignCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, session, partNumbers, callbackPtr]() mutable {
        core::application::upload::PresignPartsResult result;
        result.uploadId = session.uploadId;
        for (const auto partNumber : partNumbers)
        {
            if (partNumber <= 0 || partNumber > session.totalParts)
            {
                (*callbackPtr)(core::domain::Result<core::application::upload::PresignPartsResult>::fail(
                    core::domain::ErrorCode::InvalidArgument,
                    "invalid part_number"));
                return;
            }
            ::grpc::ClientContext grpcContext;
            addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
            ::storage_control::PresignPartReq grpcRequest;
            ::storage_control::PresignPartResp grpcResponse;
            grpcRequest.set_object_key(session.objectKey);
            grpcRequest.set_storage_upload_id(session.storageUploadId);
            grpcRequest.set_part_number(partNumber);
            grpcRequest.set_expires_seconds(kPartPresignExpirySeconds);
            const auto status = stub->PresignPart(&grpcContext, grpcRequest, &grpcResponse);
            if (!status.ok())
            {
                (*callbackPtr)(core::domain::Result<core::application::upload::PresignPartsResult>::fail(mapGrpcError(status), grpcErrorMessage(status)));
                return;
            }
            result.parts.push_back(core::application::upload::PresignedPart{partNumber, grpcResponse.url(), grpcResponse.expires_at()});
        }
        std::sort(result.parts.begin(), result.parts.end(), [](const auto &a, const auto &b) {
            return a.partNumber < b.partNumber;
        });
        (*callbackPtr)(core::domain::Result<core::application::upload::PresignPartsResult>::ok(std::move(result)));
    }).detach();
}

void GrpcStorageControlAdapter::status(const core::domain::RequestContext &ctx,
                                       const core::application::upload::UploadSession &session,
                                       StatusCallback callback)
{
    if (!requireStub<core::application::upload::UploadStatusResult>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<StatusCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, session, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
        ::storage_control::ListPartsReq grpcRequest;
        ::storage_control::ListPartsResp grpcResponse;
        grpcRequest.set_object_key(session.objectKey);
        grpcRequest.set_storage_upload_id(session.storageUploadId);
        const auto status = stub->ListParts(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<core::application::upload::UploadStatusResult>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        core::application::upload::UploadStatusResult result;
        result.totalParts = session.totalParts;
        for (const auto &part : grpcResponse.parts())
        {
            result.uploadedParts.push_back(part.part_number());
        }
        std::sort(result.uploadedParts.begin(), result.uploadedParts.end());
        (*callbackPtr)(core::domain::Result<core::application::upload::UploadStatusResult>::ok(std::move(result)));
    }).detach();
}

void GrpcStorageControlAdapter::listParts(const core::domain::RequestContext &ctx,
                                          const core::application::upload::UploadSession &session,
                                          ListPartsCallback callback)
{
    if (!requireStub<std::vector<core::application::upload::UploadedPart>>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<ListPartsCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, session, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
        ::storage_control::ListPartsReq grpcRequest;
        ::storage_control::ListPartsResp grpcResponse;
        grpcRequest.set_object_key(session.objectKey);
        grpcRequest.set_storage_upload_id(session.storageUploadId);
        const auto status = stub->ListParts(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<std::vector<core::application::upload::UploadedPart>>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        std::vector<core::application::upload::UploadedPart> parts;
        parts.reserve(grpcResponse.parts_size());
        for (const auto &part : grpcResponse.parts())
        {
            parts.push_back(core::application::upload::UploadedPart{part.part_number(), part.etag()});
        }
        std::sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) {
            return a.partNumber < b.partNumber;
        });
        (*callbackPtr)(core::domain::Result<std::vector<core::application::upload::UploadedPart>>::ok(std::move(parts)));
    }).detach();
}

void GrpcStorageControlAdapter::completeMultipart(const core::domain::RequestContext &ctx,
                                                  const core::application::upload::UploadSession &session,
                                                  const std::vector<core::application::upload::UploadedPart> &parts,
                                                  CompleteCallback callback)
{
    if (!requireStub<core::application::upload::CompleteUploadResult>(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<CompleteCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, session, parts, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kCompleteRpcDeadlineSeconds);
        ::storage_control::CompleteMultipartReq grpcRequest;
        ::storage_control::CompleteMultipartResp grpcResponse;
        grpcRequest.set_object_key(session.objectKey);
        grpcRequest.set_storage_upload_id(session.storageUploadId);
        grpcRequest.set_content_type(session.contentType.empty() ? "application/octet-stream" : session.contentType);
        for (const auto &part : parts)
        {
            auto *grpcPart = grpcRequest.add_parts();
            grpcPart->set_part_number(part.partNumber);
            grpcPart->set_etag(part.etag);
        }
        const auto status = stub->CompleteMultipart(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        core::application::upload::CompleteUploadResult result;
        result.uploadId = session.uploadId;
        result.objectKey = grpcResponse.object_key().empty() ? session.objectKey : grpcResponse.object_key();
        result.etag = grpcResponse.etag();
        result.status = "completed";
        (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::ok(std::move(result)));
    }).detach();
}

void GrpcStorageControlAdapter::abort(const core::domain::RequestContext &ctx,
                                      const core::application::upload::UploadSession &session,
                                      AbortCallback callback)
{
    if (!requireStub(stub_, callback))
    {
        return;
    }
    auto callbackPtr = std::make_shared<AbortCallback>(std::move(callback));
    auto stub = stub_;
    std::thread([stub, ctx, session, callbackPtr]() mutable {
        ::grpc::ClientContext grpcContext;
        addCommonMetadata(grpcContext, ctx, kDefaultRpcDeadlineSeconds);
        ::storage_control::AbortMultipartReq grpcRequest;
        ::storage_control::AbortMultipartResp grpcResponse;
        grpcRequest.set_object_key(session.objectKey);
        grpcRequest.set_storage_upload_id(session.storageUploadId);
        const auto status = stub->AbortMultipart(&grpcContext, grpcRequest, &grpcResponse);
        if (!status.ok())
        {
            (*callbackPtr)(core::domain::Result<void>::fail(mapGrpcError(status), grpcErrorMessage(status)));
            return;
        }
        (*callbackPtr)(core::domain::Result<void>::success());
    }).detach();
}

} // namespace core::infrastructure::storage_control
