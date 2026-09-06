#include "FileController.h"
#include "../application/file/FileDeleteService.h"
#include "../application/file/FileReadService.h"
#include "../application/upload/MultipartUploadService.h"
#include "../domain/common/RequestContext.h"
#include "../infrastructure/mysql/MysqlFileRepository.h"
#include "../infrastructure/mysql/MysqlUploadSessionRepository.h"
#include "../infrastructure/runtime/CoreRuntime.h"
#include "../infrastructure/storage_control/GrpcStorageControlAdapter.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <vector>

bool getArgumentsFromJWT(const HttpRequestPtr &req,
						 drogon::HttpResponsePtr &resp,
						 std::string &name,
						 int &userId)
{
	try
	{
		name = req->getAttributes()->get<std::string>("Name");
		userId = req->getAttributes()->get<int>("ID");
	}
	catch (const std::exception &e)
	{
		Json::Value ret;
		ret["error"] = "missing_identity";
		ret["details"] = e.what();
		resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k401Unauthorized);
		return false;
	}
	return true;
}

namespace
{
static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state = channel->GetState(true);
	return state == GRPC_CHANNEL_READY || state == GRPC_CHANNEL_IDLE || state == GRPC_CHANNEL_CONNECTING;
}

static drogon::HttpResponsePtr makeJsonResponse(const Json::Value &body, drogon::HttpStatusCode code)
{
	auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
	resp->setStatusCode(code);
	return resp;
}

static drogon::HttpResponsePtr makeErrorResponse(
	const std::string &error,
	const std::string &details,
	drogon::HttpStatusCode code)
{
	Json::Value ret;
	ret["error"] = error;
	if (!details.empty())
	{
		ret["details"] = details;
	}
	return makeJsonResponse(ret, code);
}

static drogon::HttpResponsePtr legacyFileAdapterRemoved(const std::string &flag)
{
	return makeErrorResponse("legacy_file_adapter_removed",
						 std::string("legacy file_srv adapter removed; use ") + flag + "=core",
						 k503ServiceUnavailable);
}

static drogon::HttpStatusCode httpStatusForCoreFileError(core::domain::ErrorCode code)
{
	switch (code)
	{
	case core::domain::ErrorCode::InvalidArgument:
		return k400BadRequest;
	case core::domain::ErrorCode::Unauthenticated:
		return k401Unauthorized;
	case core::domain::ErrorCode::PermissionDenied:
		return k403Forbidden;
	case core::domain::ErrorCode::NotFound:
		return k404NotFound;
	case core::domain::ErrorCode::Conflict:
	case core::domain::ErrorCode::FailedPrecondition:
		return k409Conflict;
	case core::domain::ErrorCode::DependencyUnavailable:
		return k503ServiceUnavailable;
	case core::domain::ErrorCode::Internal:
	case core::domain::ErrorCode::None:
	default:
		return k500InternalServerError;
	}
}

static bool requestContextFromJWT(const HttpRequestPtr &req,
								  drogon::HttpResponsePtr &resp,
								  core::domain::RequestContext &ctx)
{
	std::string name;
	int userId = 0;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		return false;
	}
	ctx.requestId = req->getHeader("X-Request-Id");
	ctx.username = name;
	ctx.userId = static_cast<std::uint64_t>(userId);
	return true;
}

static core::domain::RequestContext requestContextFromIdentity(const std::string &rid, const std::string &name, int userId)
{
	core::domain::RequestContext ctx;
	ctx.requestId = rid;
	ctx.username = name;
	ctx.userId = static_cast<std::uint64_t>(userId);
	return ctx;
}

static Json::Value fileListItemToJson(const core::application::file::FileListItem &item)
{
	Json::Value fileJson;
	fileJson["filename"] = item.filename;
	fileJson["filesize"] = Json::Int64(item.filesize);
	fileJson["filehash"] = item.filehash;
	fileJson["status"] = item.status;
	fileJson["scan_detail"] = item.scanDetail;
	fileJson["content_type"] = item.contentType;
	return fileJson;
}

static core::application::file::FileAccessRequest fileAccessRequestFromJson(const Json::Value &json)
{
	core::application::file::FileAccessRequest request;
	request.filename = json["filename"].asString();
	request.filehash = json["filehash"].asString();
	request.fileSize = json["file_size"].asInt64();
	return request;
}

static core::application::file::DeleteFileCommand deleteFileCommandFromJson(const Json::Value &json)
{
	core::application::file::DeleteFileCommand command;
	command.filename = json["filename"].asString();
	command.filehash = json["filehash"].asString();
	return command;
}

static core::application::upload::InitUploadCommand initUploadCommandFromJson(const Json::Value &json)
{
	core::application::upload::InitUploadCommand command;
	command.fileName = json["file_name"].asString();
	command.fileHash = json["file_hash"].asString();
	command.fileSize = json["file_size"].asInt64();
	command.contentType = json.isMember("content_type") ? json["content_type"].asString() : "application/octet-stream";
	return command;
}

static core::application::upload::PresignPartsCommand presignPartsCommandFromJson(const Json::Value &json)
{
	core::application::upload::PresignPartsCommand command;
	command.uploadId = json["upload_id"].asString();
	for (const auto &partNumber : json["part_numbers"])
	{
		command.partNumbers.push_back(partNumber.asInt());
	}
	return command;
}
} // namespace

std::shared_ptr<::storage_control::StorageControl::Stub> FileController::FindStorageControl(const std::string &key) const
{
	CloudiskConsul consul(MyAppData::instance().consulHost, MyAppData::instance().consulPort);

	return ArcGrpcLB::FindService<::storage_control::StorageControl>(
		storageControlCache_, consul, key, 10,
		[](const std::shared_ptr<grpc::Channel> &ch)
		{ return isChannelReady(ch); });
}

void FileController::filequeryinfo(const HttpRequestPtr &req,
								   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.fileReads == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_FILE_READS"));
		return;
	}

	core::domain::RequestContext requestContext;
	drogon::HttpResponsePtr authResp;
	if (!requestContextFromJWT(req, authResp, requestContext))
	{
		callback(authResp);
		return;
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlFileRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(nullptr);
	auto service = std::make_shared<core::application::file::FileReadService>(*repository, *storageControl, core::application::file::FileAuthorizationPolicy{}, runtimeSnapshot.flags);
	service->listFiles(requestContext, [callbackPtr, repository, storageControl, service, rid, username = requestContext.username](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("core_file_read_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}

		Json::Value ret;
		ret["status"] = 0;
		ret["message"] = "ok";
		ret["filelist"] = Json::Value(Json::arrayValue);
		for (const auto &file : result.value)
		{
			ret["filelist"].append(fileListItemToJson(file));
		}
		LOG_INFO_RID(rid, "[filequeryinfo] core user:{} find {} files", username, result.value.size());
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::filedowm(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.fileAccess == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_FILE_ACCESS"));
		return;
	}
	core::domain::RequestContext requestContext;
	drogon::HttpResponsePtr authResp;
	if (!requestContextFromJWT(req, authResp, requestContext))
	{
		callback(authResp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		callback(makeErrorResponse("invalid_json", "", k400BadRequest));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlFileRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::file::FileReadService>(*repository, *storageControl, core::application::file::FileAuthorizationPolicy{}, runtimeSnapshot.flags);
	auto accessRequest = fileAccessRequestFromJson(*jsonPtr);
	service->presignDownload(requestContext, accessRequest, false,
						  [callbackPtr, repository, storageControl, service, rid, filename = accessRequest.filename](auto result) {
							  if (!result.ok())
							  {
								  (*callbackPtr)(makeErrorResponse("download_unavailable", result.error.message, httpStatusForCoreFileError(result.error.code)));
								  return;
							  }
							  Json::Value ret;
							  ret["download_url"] = result.value;
							  ret["filename"] = filename;
							  LOG_INFO_RID(rid, "[filedowm] core download {}", filename);
							  (*callbackPtr)(makeJsonResponse(ret, k200OK));
						  });
}

void FileController::LoadFile(const HttpRequestPtr &req,
							  RequestStreamPtr &&streamCtx,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	(void)req;
	(void)streamCtx;
	callback(makeErrorResponse("small_upload_proxy_removed",
						   "use /file/initupload + /file/PresignParts + direct presigned PUT + /file/CompleteMultipart",
						   k503ServiceUnavailable));
}

void FileController::Showfile(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.fileAccess == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_FILE_ACCESS"));
		return;
	}
	core::domain::RequestContext requestContext;
	drogon::HttpResponsePtr authResp;
	if (!requestContextFromJWT(req, authResp, requestContext))
	{
		callback(authResp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		callback(makeErrorResponse("invalid_json", "", k400BadRequest));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlFileRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::file::FileReadService>(*repository, *storageControl, core::application::file::FileAuthorizationPolicy{}, runtimeSnapshot.flags);
	auto accessRequest = fileAccessRequestFromJson(*jsonPtr);
	service->presignDownload(requestContext, accessRequest, true,
						  [callbackPtr, repository, storageControl, service, rid, filename = accessRequest.filename](auto result) {
							  if (!result.ok())
							  {
								  (*callbackPtr)(makeErrorResponse("preview_unavailable", result.error.message, httpStatusForCoreFileError(result.error.code)));
								  return;
							  }
							  Json::Value ret;
							  ret["preview_url"] = result.value;
							  ret["message"] = result.value;
							  ret["filename"] = filename;
							  LOG_INFO_RID(rid, "[Showfile] core preview {}", filename);
							  (*callbackPtr)(makeJsonResponse(ret, k200OK));
						  });
}

void FileController::Initupload(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		callback(makeErrorResponse("invalid_json", "", k400BadRequest));
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.uploadControl == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_UPLOAD_CONTROL"));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto sessions = std::make_shared<core::infrastructure::mysql::MysqlUploadSessionRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::upload::MultipartUploadService>(*sessions, *storageControl, *sessions, runtimeSnapshot.flags);
	service->init(requestContext, initUploadCommandFromJson(*jsonPtr), [callbackPtr, sessions, storageControl, service, rid, userId](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("upload_init_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["upload_id"] = result.value.uploadId;
		ret["object_key"] = result.value.objectKey;
		ret["part_size"] = Json::Int64(result.value.partSize);
		ret["total_parts"] = result.value.totalParts;
		ret["status"] = result.value.status;
		ret["message"] = result.value.message;
		LOG_INFO_RID(rid, "[Initupload] core user:{} init {}", userId, result.value.uploadId);
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::PresignParts(const HttpRequestPtr &req,
								  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id") || !(*jsonPtr)["part_numbers"].isArray())
	{
		callback(makeErrorResponse("invalid_json", "missing upload_id/part_numbers", k400BadRequest));
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.uploadControl == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_UPLOAD_CONTROL"));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}
	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto sessions = std::make_shared<core::infrastructure::mysql::MysqlUploadSessionRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::upload::MultipartUploadService>(*sessions, *storageControl, *sessions, runtimeSnapshot.flags);
	service->presignParts(requestContext, presignPartsCommandFromJson(*jsonPtr), [callbackPtr, sessions, storageControl, service](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("presign_parts_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["upload_id"] = result.value.uploadId;
		ret["parts"] = Json::Value(Json::arrayValue);
		for (const auto &part : result.value.parts)
		{
			Json::Value item;
			item["part_number"] = part.partNumber;
			item["url"] = part.url;
			item["expires_at"] = part.expiresAt;
			ret["parts"].append(item);
		}
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::Uploadpart(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	(void)req;
	callback(makeErrorResponse("part_proxy_removed",
						   "upload parts directly to the presigned URL returned by /file/PresignParts",
						   k503ServiceUnavailable));
}

void FileController::CompleteMultipart(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		callback(makeErrorResponse("invalid_json", "missing upload_id", k400BadRequest));
		return;
	}
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.uploadComplete == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_UPLOAD_COMPLETE"));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}
	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto sessions = std::make_shared<core::infrastructure::mysql::MysqlUploadSessionRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::upload::MultipartUploadService>(*sessions, *storageControl, *sessions, runtimeSnapshot.flags);
	service->complete(requestContext, (*jsonPtr)["upload_id"].asString(), [callbackPtr, sessions, storageControl, service](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("complete_multipart_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["upload_id"] = result.value.uploadId;
		ret["object_key"] = result.value.objectKey;
		ret["etag"] = result.value.etag;
		ret["status"] = result.value.status;
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::AbortMultipart(const HttpRequestPtr &req,
									std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		callback(makeErrorResponse("invalid_json", "missing upload_id", k400BadRequest));
		return;
	}
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.uploadControl == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_UPLOAD_CONTROL"));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}
	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto sessions = std::make_shared<core::infrastructure::mysql::MysqlUploadSessionRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::upload::MultipartUploadService>(*sessions, *storageControl, *sessions, runtimeSnapshot.flags);
	service->abort(requestContext, (*jsonPtr)["upload_id"].asString(), [callbackPtr, sessions, storageControl, service](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("abort_multipart_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["status"] = "aborted";
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::Status(const HttpRequestPtr &req,
							std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		callback(makeErrorResponse("invalid_json", "missing upload_id", k400BadRequest));
		return;
	}
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.uploadControl == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_UPLOAD_CONTROL"));
		return;
	}
	auto storageStub = FindStorageControl("storage_control");
	if (!storageStub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}
	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto sessions = std::make_shared<core::infrastructure::mysql::MysqlUploadSessionRepository>(MyAppData::instance().mysqlClient);
	auto storageControl = std::make_shared<core::infrastructure::storage_control::GrpcStorageControlAdapter>(storageStub);
	auto service = std::make_shared<core::application::upload::MultipartUploadService>(*sessions, *storageControl, *sessions, runtimeSnapshot.flags);
	service->status(requestContext, (*jsonPtr)["upload_id"].asString(), [callbackPtr, sessions, storageControl, service](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("multipart_status_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["total_parts"] = result.value.totalParts;
		ret["uploaded_parts"] = Json::Value(Json::arrayValue);
		for (const auto partNumber : result.value.uploadedParts)
		{
			ret["uploaded_parts"].append(partNumber);
		}
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}

void FileController::DeleteFile(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("filename") || !(*jsonPtr).isMember("filehash"))
	{
		callback(makeErrorResponse("invalid_json", "missing filename/filehash", k400BadRequest));
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.fileDelete == core::application::ImplementationRoute::Legacy)
	{
		callback(legacyFileAdapterRemoved("CORE_FILE_DELETE"));
		return;
	}
	auto requestContext = requestContextFromIdentity(rid, name, userId);
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlFileRepository>(MyAppData::instance().mysqlClient);
	auto service = std::make_shared<core::application::file::FileDeleteService>(*repository, runtimeSnapshot.flags);
	service->deleteFile(requestContext, deleteFileCommandFromJson(*jsonPtr), [callbackPtr, repository, service](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(makeErrorResponse("delete_file_failed", result.error.message, httpStatusForCoreFileError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["status"] = 0;
		ret["message"] = result.value.objectDeleteQueued ? "deleted; object cleanup queued" : "deleted";
		ret["object_delete_queued"] = result.value.objectDeleteQueued;
		(*callbackPtr)(makeJsonResponse(ret, k200OK));
	});
}
