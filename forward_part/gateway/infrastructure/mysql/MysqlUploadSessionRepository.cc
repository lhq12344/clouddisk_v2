#include "MysqlUploadSessionRepository.h"

#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Transaction.h>

#include <memory>
#include <sstream>
#include <utility>

namespace core::infrastructure::mysql
{
namespace
{
core::application::upload::UploadSession rowToUploadSession(const drogon::orm::Row &row)
{
    core::application::upload::UploadSession session;
    session.id = row["id"].as<std::uint64_t>();
    session.uploadId = row["upload_id"].as<std::string>();
    session.ownerAccountId = row["owner_account_id"].as<std::uint64_t>();
    session.fileName = row["file_name"].as<std::string>();
    session.fileHash = row["file_hash"].as<std::string>();
    session.fileSize = row["file_size"].as<std::int64_t>();
    session.contentType = row["content_type"].isNull() ? "" : row["content_type"].as<std::string>();
    session.objectKey = row["object_key"].as<std::string>();
    session.storageUploadId = row["storage_upload_id"].as<std::string>();
    session.partSize = row["part_size"].as<std::int64_t>();
    session.totalParts = row["total_parts"].as<int>();
    session.state = core::application::upload::uploadStateFromString(row["status"].as<std::string>());
    return session;
}

std::string escapeJson(const std::string &value)
{
    std::ostringstream out;
    for (const auto ch : value)
    {
        switch (ch)
        {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
        }
    }
    return out.str();
}
} // namespace

std::string scanOutboxPayload(const core::domain::RequestContext &ctx,
                              const core::application::upload::UploadSession &session,
                              unsigned long long fileID)
{
	std::ostringstream payload;
	payload << "{\"tx_id\":\"upload:" << session.uploadId
			<< "\",\"event_id\":\"scan:" << session.uploadId
			<< "\",\"file_id\":" << fileID
            << ",\"user_id\":\"" << (ctx.userId.has_value() ? std::to_string(ctx.userId.value()) : "")
            << "\",\"sha1\":\"" << escapeJson(session.fileHash)
            << "\",\"size\":" << session.fileSize
            << ",\"oss_key\":\"" << escapeJson(session.objectKey)
            << "\",\"content_type\":\"" << escapeJson(session.contentType)
            << "\",\"event_type\":\"FILE_SCAN_REQUESTED\"}";
    return payload.str();
}

std::string requestHeadersPayload(const core::domain::RequestContext &ctx)
{
    if (ctx.requestId.empty())
    {
        return "{}";
    }
    return "{\"x-request-id\":\"" + escapeJson(ctx.requestId) + "\"}";
}

void MysqlUploadSessionRepository::saveInitiatedSession(const core::application::upload::UploadSession &session, SaveCallback callback)
{
    auto callbackPtr = std::make_shared<SaveCallback>(std::move(callback));
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                        "mysql client is not available"));
        return;
    }
    if (session.uploadId.empty() || session.objectKey.empty() || session.storageUploadId.empty())
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::InvalidArgument,
                                                        "upload session is incomplete"));
        return;
    }

    dbClient_->execSqlAsync(
        "insert into upload_sessions (created_at, updated_at, upload_id, owner_account_id, file_name, file_hash, file_size, content_type, object_key, storage_upload_id, part_size, total_parts, status, idempotency_key, expires_at, last_error) "
        "values (now(), now(), ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, date_add(now(), interval 24 hour), '') "
        "on duplicate key update updated_at=now(), owner_account_id=values(owner_account_id), file_name=values(file_name), file_hash=values(file_hash), file_size=values(file_size), content_type=values(content_type), object_key=values(object_key), storage_upload_id=values(storage_upload_id), part_size=values(part_size), total_parts=values(total_parts), status=values(status), expires_at=values(expires_at)",
        [callbackPtr](const drogon::orm::Result &) {
            (*callbackPtr)(core::domain::Result<void>::success());
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Internal, e.base().what()));
        },
        session.uploadId,
        static_cast<unsigned long long>(session.ownerAccountId),
        session.fileName,
        session.fileHash,
        static_cast<long long>(session.fileSize),
        session.contentType,
        session.objectKey,
        session.storageUploadId,
        static_cast<long long>(session.partSize),
        session.totalParts,
        core::application::upload::toString(session.state),
        session.fileHash + ":" + session.fileName);
}

void MysqlUploadSessionRepository::loadOwnedSession(const core::domain::RequestContext &ctx,
                                                    const std::string &uploadId,
                                                    LoadCallback callback)
{
    auto callbackPtr = std::make_shared<LoadCallback>(std::move(callback));
    if (!ctx.authenticated())
    {
        (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::fail(
            core::domain::ErrorCode::Unauthenticated,
            "missing authenticated request context"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "mysql client is not available"));
        return;
    }

    dbClient_->execSqlAsync(
        "select id, upload_id, owner_account_id, file_name, file_hash, file_size, content_type, object_key, storage_upload_id, part_size, total_parts, status "
        "from upload_sessions where upload_id=? and owner_account_id=? and deleted_at is null limit 1",
        [callbackPtr](const drogon::orm::Result &rows) {
            if (rows.empty())
            {
                (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::fail(
                    core::domain::ErrorCode::NotFound,
                    "upload_id not found or expired"));
                return;
            }
            (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::ok(rowToUploadSession(rows[0])));
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<core::application::upload::UploadSession>::fail(
                core::domain::ErrorCode::Internal,
                e.base().what()));
        },
        uploadId,
        static_cast<unsigned long long>(ctx.userId.value()));
}

void MysqlUploadSessionRepository::transitionState(const std::string &uploadId,
                                                   core::application::upload::UploadState expected,
                                                   core::application::upload::UploadState next,
                                                   StateCallback callback)
{
    auto callbackPtr = std::make_shared<StateCallback>(std::move(callback));
    if (!core::application::upload::canTransition(expected, next))
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::FailedPrecondition,
                                                        "invalid upload state transition"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::DependencyUnavailable,
                                                        "mysql client is not available"));
        return;
    }
    dbClient_->execSqlAsync(
        "update upload_sessions set status=?, updated_at=now() where upload_id=? and status=? and deleted_at is null",
        [callbackPtr](const drogon::orm::Result &result) {
            if (result.affectedRows() == 0)
            {
                (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Conflict,
                                                                "upload session state changed"));
                return;
            }
            (*callbackPtr)(core::domain::Result<void>::success());
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<void>::fail(core::domain::ErrorCode::Internal, e.base().what()));
        },
        core::application::upload::toString(next),
        uploadId,
        core::application::upload::toString(expected));
}

void MysqlUploadSessionRepository::finalizeCompletedUpload(const core::domain::RequestContext &ctx,
                                                           const core::application::upload::UploadSession &session,
                                                           const std::string &objectEtag,
                                                           FinalizeCallback callback)
{
    auto callbackPtr = std::make_shared<FinalizeCallback>(std::move(callback));
    if (!ctx.authenticated())
    {
        (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::fail(
            core::domain::ErrorCode::Unauthenticated,
            "missing authenticated request context"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "mysql client is not available"));
        return;
    }

    auto txDone = std::make_shared<bool>(false);
    dbClient_->newTransactionAsync([this, ctx, session, objectEtag, callbackPtr, txDone](const std::shared_ptr<drogon::orm::Transaction> &tx) {
        auto fail = [callbackPtr, txDone](core::domain::ErrorCode code, const std::string &message) {
            if (*txDone)
            {
                return;
            }
            *txDone = true;
            (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::fail(code, message));
        };
        if (!tx)
        {
            fail(core::domain::ErrorCode::DependencyUnavailable, "failed to create mysql transaction");
            return;
        }

        tx->execSqlAsync(
            "insert into files (created_at, updated_at, sha1, size, status, object_key, content_type, scan_detail) "
            "values (now(), now(), ?, ?, 'pending_scan', ?, ?, '') "
            "on duplicate key update id=last_insert_id(id), updated_at=now(), object_key=values(object_key), content_type=values(content_type), status=case when status='scan_failed' then 'pending_scan' else status end",
            [this, ctx, session, objectEtag, callbackPtr, tx, txDone, fail](const drogon::orm::Result &fileResult) mutable {
                const auto fileID = fileResult.insertId();
                tx->execSqlAsync(
                    "insert into user_files (created_at, updated_at, account_id, file_id, name) values (now(), now(), ?, ?, ?) "
                    "on duplicate key update updated_at=now(), deleted_at=null",
                    [this, ctx, session, objectEtag, callbackPtr, tx, txDone, fail, fileID](const drogon::orm::Result &) mutable {
                        tx->execSqlAsync(
                            "insert into outboxes (created_at, updated_at, event_id, tx_id, event_type, topic, `key`, payload, headers, status, retry_count, next_retry_at, last_error, locked_by) "
                            "values (now(), now(), ?, ?, 'FILE_SCAN_REQUESTED', 'file.upload.cmd', ?, ?, ?, 'NEW', 0, now(), '', '') "
                            "on duplicate key update updated_at=updated_at",
                            [this, ctx, session, objectEtag, callbackPtr, tx, txDone, fail](const drogon::orm::Result &) mutable {
                                tx->execSqlAsync(
                                    "update upload_sessions set status='pending_scan', completed_at=now(), updated_at=now(), last_error='' where upload_id=? and owner_account_id=? and deleted_at is null",
                                    [ctx, session, objectEtag, callbackPtr, txDone, fail](const drogon::orm::Result &sessionUpdate) mutable {
                                        if (*txDone)
                                        {
                                            return;
                                        }
                                        if (sessionUpdate.affectedRows() == 0)
                                        {
                                            fail(core::domain::ErrorCode::Conflict, "upload session state changed before finalize");
                                            return;
                                        }
                                        *txDone = true;
                                        core::application::upload::CompleteUploadResult result;
                                        result.uploadId = session.uploadId;
                                        result.objectKey = session.objectKey;
                                        result.etag = objectEtag;
                                        result.status = "pending_scan";
                                        (*callbackPtr)(core::domain::Result<core::application::upload::CompleteUploadResult>::ok(std::move(result)));
                                    },
                                    [fail](const drogon::orm::DrogonDbException &e) mutable {
                                        fail(core::domain::ErrorCode::Internal, e.base().what());
                                    },
                                    session.uploadId,
                                    static_cast<unsigned long long>(ctx.userId.value()));
                            },
                            [fail](const drogon::orm::DrogonDbException &e) mutable {
                                fail(core::domain::ErrorCode::Internal, e.base().what());
                            },
                            "scan:" + session.uploadId,
                            "upload:" + session.uploadId,
                            session.fileHash,
                            scanOutboxPayload(ctx, session, fileID),
                            requestHeadersPayload(ctx));
                    },
                    [fail](const drogon::orm::DrogonDbException &e) mutable {
                        fail(core::domain::ErrorCode::Internal, e.base().what());
                    },
                    static_cast<unsigned long long>(ctx.userId.value()),
                    static_cast<unsigned long long>(fileID),
                    session.fileName);
            },
            [fail](const drogon::orm::DrogonDbException &e) mutable {
                fail(core::domain::ErrorCode::Internal, e.base().what());
            },
            session.fileHash,
            static_cast<long long>(session.fileSize),
            session.objectKey,
            session.contentType);
    });
}

} // namespace core::infrastructure::mysql
