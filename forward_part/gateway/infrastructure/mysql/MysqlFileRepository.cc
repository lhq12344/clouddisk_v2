#include "MysqlFileRepository.h"

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
core::application::file::FileListItem rowToFileListItem(const drogon::orm::Row &row)
{
    core::application::file::FileListItem item;
    item.fileId = row["file_id"].as<std::uint64_t>();
    item.ownerId = row["account_id"].as<std::uint64_t>();
    item.filename = row["filename"].as<std::string>();
    item.filehash = row["filehash"].as<std::string>();
    item.filesize = row["filesize"].as<std::int64_t>();
    item.status = row["status"].isNull() ? "" : row["status"].as<std::string>();
    item.scanDetail = row["scan_detail"].isNull() ? "" : row["scan_detail"].as<std::string>();
    item.contentType = row["content_type"].isNull() ? "" : row["content_type"].as<std::string>();
    item.objectKey = row["object_key"].isNull() ? "" : row["object_key"].as<std::string>();
    item.createdAt = row["created_at"].isNull() ? "" : row["created_at"].as<std::string>();
    item.updatedAt = row["updated_at"].isNull() ? "" : row["updated_at"].as<std::string>();
    return item;
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

std::string objectDeletePayload(const core::domain::RequestContext &ctx,
                                const core::application::file::FileListItem &file)
{
    std::ostringstream payload;
    payload << "{\"tx_id\":\"delete:" << file.fileId
            << ":" << ctx.userId.value_or(0)
            << "\",\"event_id\":\"delete:" << file.fileId
            << ":" << ctx.userId.value_or(0)
            << "\",\"file_id\":" << file.fileId
            << ",\"user_id\":\"" << ctx.userId.value_or(0)
            << "\",\"sha1\":\"" << escapeJson(file.filehash)
            << "\",\"size\":" << file.filesize
            << ",\"oss_key\":\"" << escapeJson(file.objectKey)
            << "\",\"content_type\":\"" << escapeJson(file.contentType)
            << "\",\"event_type\":\"OBJECT_DELETE_REQUESTED\"}";
    return payload.str();
}

std::string headersPayload(const core::domain::RequestContext &ctx)
{
    if (ctx.requestId.empty())
    {
        return "{}";
    }
    return "{\"x-request-id\":\"" + escapeJson(ctx.requestId) + "\"}";
}
} // namespace

void MysqlFileRepository::listOwnedFiles(const core::domain::RequestContext &ctx, ListCallback callback)
{
    auto callbackPtr = std::make_shared<ListCallback>(std::move(callback));
    if (!ctx.authenticated())
    {
        (*callbackPtr)(core::domain::Result<std::vector<core::application::file::FileListItem>>::fail(
            core::domain::ErrorCode::Unauthenticated,
            "missing authenticated request context"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<std::vector<core::application::file::FileListItem>>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "mysql client is not available"));
        return;
    }

    dbClient_->execSqlAsync(
        "select uf.account_id, uf.file_id, uf.name as filename, f.sha1 as filehash, f.size as filesize, "
        "f.status, f.scan_detail, f.content_type, f.object_key, uf.created_at, uf.updated_at "
        "from user_files uf join files f on f.id = uf.file_id and f.deleted_at is null "
        "where uf.account_id = ? and uf.deleted_at is null order by uf.id desc",
        [callbackPtr](const drogon::orm::Result &rows) {
            std::vector<core::application::file::FileListItem> files;
            files.reserve(rows.size());
            for (const auto &row : rows)
            {
                files.push_back(rowToFileListItem(row));
            }
            (*callbackPtr)(core::domain::Result<std::vector<core::application::file::FileListItem>>::ok(std::move(files)));
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<std::vector<core::application::file::FileListItem>>::fail(
                core::domain::ErrorCode::Internal,
                e.base().what()));
        },
        static_cast<unsigned long long>(ctx.userId.value()));
}

void MysqlFileRepository::resolveOwnedFile(const core::domain::RequestContext &ctx,
                                           const core::application::file::FileAccessRequest &request,
                                           ResolveCallback callback)
{
    auto callbackPtr = std::make_shared<ResolveCallback>(std::move(callback));
    if (!ctx.authenticated())
    {
        (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::fail(
            core::domain::ErrorCode::Unauthenticated,
            "missing authenticated request context"));
        return;
    }
    if (request.filename.empty() && request.filehash.empty())
    {
        (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::fail(
            core::domain::ErrorCode::InvalidArgument,
            "filename or filehash is required"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "mysql client is not available"));
        return;
    }

    dbClient_->execSqlAsync(
        "select uf.account_id, uf.file_id, uf.name as filename, f.sha1 as filehash, f.size as filesize, "
        "f.status, f.scan_detail, f.content_type, f.object_key, uf.created_at, uf.updated_at "
        "from user_files uf join files f on f.id = uf.file_id and f.deleted_at is null "
        "where uf.account_id = ? and uf.deleted_at is null "
        "and (? = '' or uf.name = ?) and (? = '' or f.sha1 = ?) order by uf.id desc limit 1",
        [callbackPtr](const drogon::orm::Result &rows) {
            if (rows.empty())
            {
                (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::fail(
                    core::domain::ErrorCode::NotFound,
                    "file not found"));
                return;
            }
            (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::ok(rowToFileListItem(rows[0])));
        },
        [callbackPtr](const drogon::orm::DrogonDbException &e) {
            (*callbackPtr)(core::domain::Result<core::application::file::FileListItem>::fail(
                core::domain::ErrorCode::Internal,
                e.base().what()));
        },
        static_cast<unsigned long long>(ctx.userId.value()),
        request.filename,
        request.filename,
        request.filehash,
        request.filehash);
}

void MysqlFileRepository::deleteOwnedFile(const core::domain::RequestContext &ctx,
                                          const core::application::file::DeleteFileCommand &command,
                                          DeleteCallback callback)
{
    auto callbackPtr = std::make_shared<DeleteCallback>(std::move(callback));
    if (!ctx.authenticated())
    {
        (*callbackPtr)(core::domain::Result<core::application::file::DeleteFileResult>::fail(
            core::domain::ErrorCode::Unauthenticated,
            "missing authenticated request context"));
        return;
    }
    if (!dbClient_)
    {
        (*callbackPtr)(core::domain::Result<core::application::file::DeleteFileResult>::fail(
            core::domain::ErrorCode::DependencyUnavailable,
            "mysql client is not available"));
        return;
    }

    dbClient_->newTransactionAsync([ctx, command, callbackPtr](const std::shared_ptr<drogon::orm::Transaction> &tx) {
        auto fail = [callbackPtr](core::domain::ErrorCode code, const std::string &message) {
            (*callbackPtr)(core::domain::Result<core::application::file::DeleteFileResult>::fail(code, message));
        };
        if (!tx)
        {
            fail(core::domain::ErrorCode::DependencyUnavailable, "failed to create mysql transaction");
            return;
        }

        tx->execSqlAsync(
            "select uf.account_id, uf.file_id, uf.name as filename, f.sha1 as filehash, f.size as filesize, "
            "f.status, f.scan_detail, f.content_type, f.object_key, uf.created_at, uf.updated_at "
            "from user_files uf join files f on f.id = uf.file_id and f.deleted_at is null "
            "where uf.account_id=? and uf.deleted_at is null and uf.name=? and (? = '' or f.sha1=?) order by uf.id desc limit 1 for update",
            [ctx, command, callbackPtr, tx, fail](const drogon::orm::Result &rows) mutable {
                if (rows.empty())
                {
                    fail(core::domain::ErrorCode::NotFound, "file not found");
                    return;
                }
                auto file = std::make_shared<core::application::file::FileListItem>(rowToFileListItem(rows[0]));
                if (file->status == "pending_scan")
                {
                    fail(core::domain::ErrorCode::Conflict, "file is pending virus scan");
                    return;
                }
                if (file->objectKey.empty() && !file->filehash.empty())
                {
                    file->objectKey = "files/" + file->filehash;
                }

                tx->execSqlAsync(
                    "delete from user_files where account_id=? and file_id=? and name=?",
                    [ctx, callbackPtr, tx, fail, file](const drogon::orm::Result &) mutable {
                        tx->execSqlAsync(
                            "select count(*) as remaining from user_files where file_id=? and deleted_at is null",
                            [ctx, callbackPtr, tx, fail, file](const drogon::orm::Result &countRows) mutable {
                                const auto remaining = countRows.empty() ? 0 : countRows[0]["remaining"].as<int>();
                                if (remaining > 0 || file->objectKey.empty())
                                {
                                    core::application::file::DeleteFileResult result;
                                    result.objectDeleteQueued = false;
                                    result.fileId = file->fileId;
                                    result.objectKey = file->objectKey;
                                    (*callbackPtr)(core::domain::Result<core::application::file::DeleteFileResult>::ok(std::move(result)));
                                    return;
                                }

                                tx->execSqlAsync(
                                    "insert into outboxes (created_at, updated_at, event_id, tx_id, event_type, topic, `key`, payload, headers, status, retry_count, next_retry_at, last_error, locked_by) "
                                    "values (now(), now(), ?, ?, 'OBJECT_DELETE_REQUESTED', 'file.upload.cmd', ?, ?, ?, 'NEW', 0, now(), '', '') "
                                    "on duplicate key update updated_at=updated_at",
                                    [callbackPtr, file](const drogon::orm::Result &) mutable {
                                        core::application::file::DeleteFileResult result;
                                        result.objectDeleteQueued = true;
                                        result.fileId = file->fileId;
                                        result.objectKey = file->objectKey;
                                        (*callbackPtr)(core::domain::Result<core::application::file::DeleteFileResult>::ok(std::move(result)));
                                    },
                                    [fail](const drogon::orm::DrogonDbException &e) mutable {
                                        fail(core::domain::ErrorCode::Internal, e.base().what());
                                    },
                                    "delete:" + std::to_string(file->fileId) + ":" + std::to_string(ctx.userId.value()),
                                    "delete:" + std::to_string(file->fileId) + ":" + std::to_string(ctx.userId.value()),
                                    file->filehash,
                                    objectDeletePayload(ctx, *file),
                                    headersPayload(ctx));
                            },
                            [fail](const drogon::orm::DrogonDbException &e) mutable {
                                fail(core::domain::ErrorCode::Internal, e.base().what());
                            },
                            static_cast<unsigned long long>(file->fileId));
                    },
                    [fail](const drogon::orm::DrogonDbException &e) mutable {
                        fail(core::domain::ErrorCode::Internal, e.base().what());
                    },
                    static_cast<unsigned long long>(ctx.userId.value()),
                    static_cast<unsigned long long>(file->fileId),
                    file->filename);
            },
            [fail](const drogon::orm::DrogonDbException &e) mutable {
                fail(core::domain::ErrorCode::Internal, e.base().what());
            },
            static_cast<unsigned long long>(ctx.userId.value()),
            command.filename,
            command.filehash,
            command.filehash);
    });
}

} // namespace core::infrastructure::mysql
