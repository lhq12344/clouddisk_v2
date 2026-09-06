#pragma once

#include "UploadPorts.h"
#include "../common/FeatureFlags.h"

#include <memory>
#include <utility>

namespace core::application::upload
{

class MultipartUploadService
{
public:
    using InitCallback = std::function<void(core::domain::Result<InitUploadResult>)>;
    using PresignCallback = MultipartStorageControlPort::PresignCallback;
    using StatusCallback = MultipartStorageControlPort::StatusCallback;
    using CompleteCallback = UploadFinalizationPort::FinalizeCallback;
    using AbortCallback = MultipartStorageControlPort::AbortCallback;

    MultipartUploadService(UploadSessionPort &sessions,
                           MultipartStorageControlPort &storageControl,
                           UploadFinalizationPort &finalizer,
                           CoreFeatureFlags flags)
        : sessions_(sessions), storageControl_(storageControl), finalizer_(finalizer), flags_(flags) {}

    bool shouldUseCoreUploadControlPath() const
    {
        return flags_.uploadControl == ImplementationRoute::Core || flags_.uploadControl == ImplementationRoute::Shadow;
    }

    void init(const core::domain::RequestContext &ctx, InitUploadCommand command, InitCallback callback)
    {
        if (!ctx.authenticated())
        {
            callback(core::domain::Result<InitUploadResult>::fail(core::domain::ErrorCode::Unauthenticated,
                                                                  "missing authenticated request context"));
            return;
        }
        if (command.fileName.empty() || command.fileHash.empty() || command.fileSize <= 0)
        {
            callback(core::domain::Result<InitUploadResult>::fail(core::domain::ErrorCode::InvalidArgument,
                                                                  "missing file_name/file_hash or invalid file_size"));
            return;
        }

        storageControl_.initiateMultipart(ctx, command,
                                          [this, ctx, callback = std::move(callback)](auto sessionResult) mutable {
                                              if (!sessionResult.ok())
                                              {
                                                  callback(core::domain::Result<InitUploadResult>::fail(sessionResult.error.code, sessionResult.error.message));
                                                  return;
                                              }
                                              auto session = std::make_shared<UploadSession>(std::move(sessionResult.value));
                                              sessions_.saveInitiatedSession(*session,
                                                                            [this, ctx, session, callback = std::move(callback)](auto saveResult) mutable {
                                                                                if (!saveResult.ok())
                                                                                {
                                                                                    const auto error = saveResult.error;
                                                                                    storageControl_.abort(ctx, *session,
                                                                                                          [callback = std::move(callback), error](auto) mutable {
                                                                                                              callback(core::domain::Result<InitUploadResult>::fail(error.code, error.message));
                                                                                                          });
                                                                                    return;
                                                                                }
                                                                                InitUploadResult result;
                                                                                result.uploadId = session->uploadId;
                                                                                result.objectKey = session->objectKey;
                                                                                result.partSize = session->partSize;
                                                                                result.totalParts = session->totalParts;
                                                                                result.status = toString(session->state);
                                                                                result.message = "multipart initialized";
                                                                                callback(core::domain::Result<InitUploadResult>::ok(std::move(result)));
                                                                            });
                                          });
    }

    void presignParts(const core::domain::RequestContext &ctx,
                      PresignPartsCommand command,
                      PresignCallback callback)
    {
        if (!requireAuthenticated(ctx, callback) || !requireUploadId(command.uploadId, callback))
        {
            return;
        }
        auto commandPtr = std::make_shared<PresignPartsCommand>(std::move(command));
        sessions_.loadOwnedSession(ctx, commandPtr->uploadId,
                                   [this, ctx, commandPtr, callback = std::move(callback)](auto sessionResult) mutable {
                                       if (!sessionResult.ok())
                                       {
                                           callback(core::domain::Result<PresignPartsResult>::fail(sessionResult.error.code, sessionResult.error.message));
                                           return;
                                       }
                                       storageControl_.presignParts(ctx, sessionResult.value, commandPtr->partNumbers, std::move(callback));
                                   });
    }

    void status(const core::domain::RequestContext &ctx, std::string uploadId, StatusCallback callback)
    {
        if (!requireAuthenticated(ctx, callback) || !requireUploadId(uploadId, callback))
        {
            return;
        }
        sessions_.loadOwnedSession(ctx, uploadId,
                                   [this, ctx, callback = std::move(callback)](auto sessionResult) mutable {
                                       if (!sessionResult.ok())
                                       {
                                           callback(core::domain::Result<UploadStatusResult>::fail(sessionResult.error.code, sessionResult.error.message));
                                           return;
                                       }
                                       storageControl_.status(ctx, sessionResult.value, std::move(callback));
                                   });
    }

    void abort(const core::domain::RequestContext &ctx, std::string uploadId, AbortCallback callback)
    {
        if (!requireAuthenticated(ctx, callback) || !requireUploadId(uploadId, callback))
        {
            return;
        }
        sessions_.loadOwnedSession(ctx, uploadId,
                                   [this, ctx, callback = std::move(callback)](auto sessionResult) mutable {
                                       if (!sessionResult.ok())
                                       {
                                           callback(core::domain::Result<void>::fail(sessionResult.error.code, sessionResult.error.message));
                                           return;
                                       }
                                       auto session = std::make_shared<UploadSession>(std::move(sessionResult.value));
                                       storageControl_.abort(ctx, *session,
                                                             [this, session, callback = std::move(callback)](auto abortResult) mutable {
                                                                 if (!abortResult.ok())
                                                                 {
                                                                     callback(std::move(abortResult));
                                                                     return;
                                                                 }
                                                                 sessions_.transitionState(session->uploadId, session->state, UploadState::Aborted,
                                                                                           [callback = std::move(callback)](auto transitionResult) mutable {
                                                                                               if (!transitionResult.ok())
                                                                                               {
                                                                                                   callback(core::domain::Result<void>::success());
                                                                                                   return;
                                                                                               }
                                                                                               callback(std::move(transitionResult));
                                                                                           });
                                                             });
                                   });
    }

    void complete(const core::domain::RequestContext &ctx, std::string uploadId, CompleteCallback callback)
    {
        if (!requireAuthenticated(ctx, callback) || !requireUploadId(uploadId, callback))
        {
            return;
        }
        sessions_.loadOwnedSession(ctx, uploadId,
                                   [this, ctx, callback = std::move(callback)](auto sessionResult) mutable {
                                       if (!sessionResult.ok())
                                       {
                                           callback(core::domain::Result<CompleteUploadResult>::fail(sessionResult.error.code, sessionResult.error.message));
                                           return;
                                       }
                                       auto session = std::make_shared<UploadSession>(std::move(sessionResult.value));
                                       if (session->state == UploadState::Ready || session->state == UploadState::PendingScan)
                                       {
                                           CompleteUploadResult result;
                                           result.uploadId = session->uploadId;
                                           result.objectKey = session->objectKey;
                                           result.status = toString(session->state);
                                           callback(core::domain::Result<CompleteUploadResult>::ok(std::move(result)));
                                           return;
                                       }
                                       storageControl_.listParts(ctx, *session,
                                                                 [this, ctx, session, callback = std::move(callback)](auto partsResult) mutable {
                                                                     if (!partsResult.ok())
                                                                     {
                                                                         callback(core::domain::Result<CompleteUploadResult>::fail(partsResult.error.code, partsResult.error.message));
                                                                         return;
                                                                     }
                                                                     if (static_cast<int>(partsResult.value.size()) != session->totalParts)
                                                                     {
                                                                         callback(core::domain::Result<CompleteUploadResult>::fail(core::domain::ErrorCode::FailedPrecondition,
                                                                                                                                    "parts not complete"));
                                                                         return;
                                                                     }
                                                                     auto parts = std::make_shared<std::vector<UploadedPart>>(std::move(partsResult.value));
                                                                     sessions_.transitionState(session->uploadId, session->state, UploadState::Completing,
                                                                                               [this, ctx, session, parts, callback = std::move(callback)](auto transitionResult) mutable {
                                                                                                   if (!transitionResult.ok())
                                                                                                   {
                                                                                                       if (transitionResult.error.code == core::domain::ErrorCode::Conflict)
                                                                                                       {
                                                                                                           sessions_.loadOwnedSession(ctx, session->uploadId,
                                                                                                                                      [callback = std::move(callback)](auto latestResult) mutable {
                                                                                                                                          if (latestResult.ok() &&
                                                                                                                                              (latestResult.value.state == UploadState::Ready || latestResult.value.state == UploadState::PendingScan))
                                                                                                                                          {
                                                                                                                                              CompleteUploadResult result;
                                                                                                                                              result.uploadId = latestResult.value.uploadId;
                                                                                                                                              result.objectKey = latestResult.value.objectKey;
                                                                                                                                              result.status = toString(latestResult.value.state);
                                                                                                                                              callback(core::domain::Result<CompleteUploadResult>::ok(std::move(result)));
                                                                                                                                              return;
                                                                                                                                          }
                                                                                                                                          if (latestResult.ok() && latestResult.value.state == UploadState::Completing)
                                                                                                                                          {
                                                                                                                                              callback(core::domain::Result<CompleteUploadResult>::fail(core::domain::ErrorCode::Conflict,
                                                                                                                                                                                                       "upload completion already in progress"));
                                                                                                                                              return;
                                                                                                                                          }
                                                                                                                                          if (!latestResult.ok())
                                                                                                                                          {
                                                                                                                                              callback(core::domain::Result<CompleteUploadResult>::fail(latestResult.error.code, latestResult.error.message));
                                                                                                                                              return;
                                                                                                                                          }
                                                                                                                                          callback(core::domain::Result<CompleteUploadResult>::fail(core::domain::ErrorCode::Conflict,
                                                                                                                                                                                                   "upload session state changed"));
                                                                                                                                      });
                                                                                                           return;
                                                                                                       }
                                                                                                       callback(core::domain::Result<CompleteUploadResult>::fail(transitionResult.error.code, transitionResult.error.message));
                                                                                                       return;
                                                                                                   }
                                                                                                   storageControl_.completeMultipart(ctx, *session, *parts,
                                                                                                                                     [this, ctx, session, callback = std::move(callback)](auto completeResult) mutable {
                                                                                                                                         if (!completeResult.ok())
                                                                                                                                         {
                                                                                                                                             callback(std::move(completeResult));
                                                                                                                                             return;
                                                                                                                                         }
                                                                                                                                         finalizer_.finalizeCompletedUpload(ctx, *session, completeResult.value.etag, std::move(callback));
                                                                                                                                     });
                                                                                               });
                                                                 });
                                   });
    }

private:
    template <typename T>
    bool requireAuthenticated(const core::domain::RequestContext &ctx, std::function<void(core::domain::Result<T>)> &callback)
    {
        if (ctx.authenticated())
        {
            return true;
        }
        callback(core::domain::Result<T>::fail(core::domain::ErrorCode::Unauthenticated,
                                               "missing authenticated request context"));
        return false;
    }

    bool requireAuthenticated(const core::domain::RequestContext &ctx, std::function<void(core::domain::Result<void>)> &callback)
    {
        if (ctx.authenticated())
        {
            return true;
        }
        callback(core::domain::Result<void>::fail(core::domain::ErrorCode::Unauthenticated,
                                                 "missing authenticated request context"));
        return false;
    }

    template <typename T>
    bool requireUploadId(const std::string &uploadId, std::function<void(core::domain::Result<T>)> &callback)
    {
        if (!uploadId.empty())
        {
            return true;
        }
        callback(core::domain::Result<T>::fail(core::domain::ErrorCode::InvalidArgument,
                                               "missing upload_id"));
        return false;
    }

    bool requireUploadId(const std::string &uploadId, std::function<void(core::domain::Result<void>)> &callback)
    {
        if (!uploadId.empty())
        {
            return true;
        }
        callback(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                 "missing upload_id"));
        return false;
    }

    UploadSessionPort &sessions_;
    MultipartStorageControlPort &storageControl_;
    UploadFinalizationPort &finalizer_;
    CoreFeatureFlags flags_;
};

} // namespace core::application::upload
