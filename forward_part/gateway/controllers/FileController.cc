#include "FileController.h"
#include "GrpcHttp.h"

#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
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

static bool parsePositiveInt(const std::string &s, int &out)
{
	if (s.empty())
	{
		return false;
	}
	char *end = nullptr;
	long v = std::strtol(s.c_str(), &end, 10);
	if (!end || *end != '\0' || v <= 0 || v > INT32_MAX)
	{
		return false;
	}
	out = static_cast<int>(v);
	return true;
}

static bool parseNonNegativeInt64(const std::string &s, int64_t &out)
{
	if (s.empty())
	{
		return false;
	}
	char *end = nullptr;
	long long v = std::strtoll(s.c_str(), &end, 10);
	if (!end || *end != '\0' || v < 0)
	{
		return false;
	}
	out = static_cast<int64_t>(v);
	return true;
}

struct PresignedUploadTarget
{
	std::string hostString;
	std::string path;
	std::vector<std::pair<std::string, std::string>> queryParameters;
};

static bool parsePresignedUploadUrl(const std::string &rawUrl,
									PresignedUploadTarget &target,
									std::string &error)
{
	static const std::regex kUrlPattern(R"(^(https?)://([^/?#]+)(/[^#]*)?$)",
										std::regex::icase);
	std::smatch match;
	if (!std::regex_match(rawUrl, match, kUrlPattern))
	{
		error = "invalid presigned url";
		return false;
	}

	auto scheme = match[1].str();
	auto authority = match[2].str();
	auto pathAndQuery = match[3].matched ? match[3].str() : "/";
	auto path = pathAndQuery;
	auto queryPos = pathAndQuery.find('?');
	if (queryPos != std::string::npos)
	{
		path = pathAndQuery.substr(0, queryPos);
		auto query = pathAndQuery.substr(queryPos + 1);
		size_t start = 0;
		while (start <= query.size())
		{
			auto amp = query.find('&', start);
			auto token = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
			if (!token.empty())
			{
				auto eq = token.find('=');
				auto key = token.substr(0, eq);
				auto value = eq == std::string::npos ? std::string() : token.substr(eq + 1);
				target.queryParameters.emplace_back(drogon::utils::urlDecode(key),
													drogon::utils::urlDecode(value));
			}
			if (amp == std::string::npos)
			{
				break;
			}
			start = amp + 1;
		}
	}

	std::string host = authority;
	if (!host.empty() && host.front() == '[')
	{
		auto pos = host.find(']');
		if (pos == std::string::npos)
		{
			error = "invalid presigned url host";
			return false;
		}
		host = host.substr(1, pos - 1);
	}
	else
	{
		auto pos = host.rfind(':');
		if (pos != std::string::npos)
		{
			host = host.substr(0, pos);
		}
	}

	std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	const bool allowedHost = host == "127.0.0.1" ||
							 host == "localhost" ||
							 host.rfind("10.42.", 0) == 0;
	if (!allowedHost)
	{
		error = "presigned upload host not allowed";
		return false;
	}

	if (path.find("/clouddisk/") != 0 ||
		pathAndQuery.find("X-Amz-Algorithm=") == std::string::npos ||
		pathAndQuery.find("uploadId=") == std::string::npos ||
		pathAndQuery.find("partNumber=") == std::string::npos)
	{
		error = "invalid presigned upload path";
		return false;
	}

	target.hostString = scheme + "://" + authority;
	target.path = path;
	return true;
}

class UploadFileProxy : public std::enable_shared_from_this<UploadFileProxy>
{
public:
	static void Start(std::shared_ptr<file::fileService::Stub> stub,
					  const HttpRequestPtr &req,
					  RequestStreamPtr &&streamCtx,
					  int userId,
					  std::string fileName,
					  std::string fileHash,
					  int64_t fileSize,
					  std::string contentType,
					  std::string requestId,
					  std::function<void(const HttpResponsePtr &)> &&callback)
	{
		auto session = std::shared_ptr<UploadFileProxy>(new UploadFileProxy(
			std::move(stub),
			req,
			userId,
			std::move(fileName),
			std::move(fileHash),
			fileSize,
			std::move(contentType),
			std::move(requestId),
			std::move(callback)));
		session->begin(std::move(streamCtx));
	}

private:
	UploadFileProxy(std::shared_ptr<file::fileService::Stub> stub,
					HttpRequestPtr req,
					int userId,
					std::string fileName,
					std::string fileHash,
					int64_t fileSize,
					std::string contentType,
					std::string requestId,
					std::function<void(const HttpResponsePtr &)> &&callback)
		: stub_(std::move(stub)),
		  req_(std::move(req)),
		  userId_(userId),
		  fileName_(std::move(fileName)),
		  fileHash_(std::move(fileHash)),
		  fileSize_(fileSize),
		  contentType_(std::move(contentType)),
		  rid_(std::move(requestId)),
		  callback_(std::move(callback))
	{
	}

	void begin(RequestStreamPtr &&streamCtx)
	{
		streamCtx_ = std::move(streamCtx);
		auto weak = weak_from_this();
		auto reader = RequestStreamReader::newReader(
			[weak](const char *data, size_t len) {
				if (auto self = weak.lock())
				{
					self->onData(data, len);
				}
			},
			[weak](std::exception_ptr ep) {
				if (auto self = weak.lock())
				{
					self->onFinish(ep);
				}
			});
		worker_ = std::thread([self = shared_from_this()]() { self->run(); });
		worker_.detach();
		streamCtx_->setStreamReader(reader);
	}

	void onData(const char *data, size_t len)
	{
		if (len == 0)
		{
			return;
		}
		{
			std::lock_guard<std::mutex> lk(mu_);
			chunks_.emplace_back(data, len);
		}
		cv_.notify_one();
	}

	void onFinish(std::exception_ptr ep)
	{
		if (ep)
		{
			try
			{
				std::rethrow_exception(ep);
			}
			catch (const std::exception &ex)
			{
				finishError_ = ex.what();
			}
			catch (...)
			{
				finishError_ = "request stream aborted";
			}
		}
		{
			std::lock_guard<std::mutex> lk(mu_);
			finished_ = true;
		}
		cv_.notify_one();
	}

	void respond(const HttpResponsePtr &resp)
	{
		drogon::app().getLoop()->queueInLoop([callback = callback_, resp]() { callback(resp); });
	}

	void run()
	{
		grpc::ClientContext context;
		if (!rid_.empty())
		{
			context.AddMetadata("x-request-id", rid_);
		}
		context.AddMetadata("x-user-id", std::to_string(userId_));

		file::UploadFileResp grpcResp;
		auto writer = stub_->UploadFile(&context, &grpcResp);
		if (!writer)
		{
			respond(makeErrorResponse("grpc_error", "failed to open upload stream", k500InternalServerError));
			return;
		}

		file::UploadFileReq metaReq;
		auto *meta = metaReq.mutable_meta();
		meta->set_user_id(std::to_string(userId_));
		meta->set_file_name(fileName_);
		meta->set_file_hash(fileHash_);
		meta->set_file_size(fileSize_);
		meta->set_content_type(contentType_);
		if (!writer->Write(metaReq))
		{
			respond(makeErrorResponse("grpc_error", "failed to send upload metadata", k500InternalServerError));
			return;
		}

		for (;;)
		{
			std::string chunk;
			bool shouldFinish = false;
			{
				std::unique_lock<std::mutex> lk(mu_);
				cv_.wait(lk, [this]() { return !chunks_.empty() || finished_; });
				if (!chunks_.empty())
				{
					chunk = std::move(chunks_.front());
					chunks_.pop_front();
				}
				else if (finished_)
				{
					shouldFinish = true;
				}
			}

			if (!chunk.empty())
			{
				file::UploadFileReq dataReq;
				dataReq.set_data(chunk.data(), chunk.size());
				if (!writer->Write(dataReq))
				{
					respond(makeErrorResponse("grpc_error", "failed to forward upload chunk", k500InternalServerError));
					return;
				}
				continue;
			}

			if (shouldFinish)
			{
				break;
			}
		}

		if (!finishError_.empty())
		{
			context.TryCancel();
			writer->WritesDone();
			writer->Finish();
			respond(makeErrorResponse("stream_error", finishError_, k400BadRequest));
			return;
		}

		writer->WritesDone();
		const auto grpcStatus = writer->Finish();
		if (!grpcStatus.ok())
		{
			respond(grpcErrorResponse(grpcStatus));
			return;
		}

		Json::Value ret;
		ret["file_hash"] = grpcResp.file_hash();
		ret["object_key"] = grpcResp.object_key();
		ret["status"] = grpcResp.status();
		ret["message"] = grpcResp.message();
		auto httpStatus = grpcResp.status() == "infected" ? k409Conflict : k200OK;
		respond(makeJsonResponse(ret, httpStatus));
	}

	std::shared_ptr<file::fileService::Stub> stub_;
	HttpRequestPtr req_;
	RequestStreamPtr streamCtx_;
	int userId_;
	std::string fileName_;
	std::string fileHash_;
	int64_t fileSize_;
	std::string contentType_;
	std::string rid_;
	std::function<void(const HttpResponsePtr &)> callback_;

	std::mutex mu_;
	std::condition_variable cv_;
	std::deque<std::string> chunks_;
	bool finished_{false};
	std::string finishError_;
	std::thread worker_;
};
} // namespace

std::shared_ptr<file::fileService::Stub> FileController::FindService(const std::string &key) const
{
	CloudiskConsul consul(MyAppData::instance().consulHost, MyAppData::instance().consulPort);

	return ArcGrpcLB::FindService<file::fileService>(
		cache_, consul, key, 10,
		[](const std::shared_ptr<grpc::Channel> &ch)
		{ return isChannelReady(ch); });
}

void FileController::filequeryinfo(const HttpRequestPtr &req,
								   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::ReqFileQuery>();
	auto response = std::make_shared<::file::RespFileQuery>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	context->AddMetadata("x-request-id", rid);

	stub->async()->filequeryinfo(context.get(), request.get(), response.get(),
								 [context, request, response, callback, rid](::grpc::Status status) {
									 if (response == nullptr)
									 {
										 callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
										 return;
									 }
									 if (!status.ok())
									 {
										 callback(grpcErrorResponse(status));
										 return;
									 }
									 Json::Value ret;
									 ret["status"] = response->code();
									 ret["message"] = response->message();
									 ret["filelist"] = Json::Value(Json::arrayValue);
									 for (int i = 0; i < response->files_size(); ++i)
									 {
										 const auto &fileinfo = response->files(i);
										 Json::Value fileJson;
										 fileJson["filename"] = fileinfo.file_name();
										 fileJson["filesize"] = Json::Int64(fileinfo.file_sizes());
										 fileJson["filehash"] = fileinfo.file_hash();
										 fileJson["status"] = fileinfo.status();
										 fileJson["scan_detail"] = fileinfo.scan_detail();
										 fileJson["content_type"] = fileinfo.content_type();
										 ret["filelist"].append(fileJson);
									 }
									 LOG_INFO_RID(rid, "[filequeryinfo] user:{} find {} files", request->username(), response->files_size());
									 callback(makeJsonResponse(ret, k200OK));
								 });
}

void FileController::filedowm(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::ReqFileDown>();
	auto response = std::make_shared<::file::Resp>();

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
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	context->AddMetadata("x-request-id", rid);

	stub->async()->filedowm(context.get(), request.get(), response.get(),
							[context, request, response, callback, rid](::grpc::Status status) {
								if (response == nullptr)
								{
									callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
									return;
								}
								if (!status.ok())
								{
									callback(grpcErrorResponse(status));
									return;
								}
								if (response->code() != 0)
								{
									callback(makeErrorResponse("download_unavailable", response->message(), k409Conflict));
									return;
								}
								Json::Value ret;
								ret["download_url"] = response->message();
								ret["filename"] = request->filename();
								LOG_INFO_RID(rid, "[filedowm] user:{} download {}", request->username(), request->filename());
								callback(makeJsonResponse(ret, k200OK));
							});
}

void FileController::LoadFile(const HttpRequestPtr &req,
							  RequestStreamPtr &&streamCtx,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
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

	const auto fileName = req->getHeader("X-File-Name");
	const auto fileHash = req->getHeader("X-File-Hash");
	const auto fileSizeHeader = req->getHeader("X-File-Size");
	const auto contentType = req->getHeader("Content-Type");
	int64_t fileSize = 0;
	if (fileName.empty() || fileHash.empty() || !parseNonNegativeInt64(fileSizeHeader, fileSize))
	{
		callback(makeErrorResponse("invalid_request", "missing or invalid X-File-Name/X-File-Hash/X-File-Size", k400BadRequest));
		return;
	}

	UploadFileProxy::Start(
		stub,
		req,
		std::move(streamCtx),
		userId,
		fileName,
		fileHash,
		fileSize,
		contentType.empty() ? "application/octet-stream" : contentType,
		rid,
		std::move(callback));
}

void FileController::Showfile(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::Reqshowfile>();
	auto response = std::make_shared<::file::Resp>();

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
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	context->AddMetadata("x-request-id", rid);

	stub->async()->Showfile(context.get(), request.get(), response.get(),
							[context, request, response, callback, rid](::grpc::Status status) {
								if (response == nullptr)
								{
									callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
									return;
								}
								if (!status.ok())
								{
									callback(grpcErrorResponse(status));
									return;
								}
								if (response->code() != 0)
								{
									callback(makeErrorResponse("preview_unavailable", response->message(), k409Conflict));
									return;
								}
								Json::Value ret;
								ret["preview_url"] = response->message();
								ret["filename"] = request->filename();
								LOG_INFO_RID(rid, "[Showfile] user:{} preview {}", request->username(), request->filename());
								callback(makeJsonResponse(ret, k200OK));
							});
}

void FileController::Initupload(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::InitReq>();
	auto response = std::make_shared<::file::InitResp>();

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

	request->set_file_name((*jsonPtr)["file_name"].asString());
	request->set_file_hash((*jsonPtr)["file_hash"].asString());
	request->set_user_id(std::to_string(userId));
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	if ((*jsonPtr).isMember("content_type"))
	{
		request->set_content_type((*jsonPtr)["content_type"].asString());
		}
		context->AddMetadata("x-request-id", rid);
		context->AddMetadata("x-user-id", std::to_string(userId));

		stub->async()->InitMultipart(context.get(), request.get(), response.get(),
									 [context, request, response, callback, rid](::grpc::Status status) {
										 if (response == nullptr)
										 {
											 callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
											 return;
										 }
										 if (!status.ok())
										 {
											 callback(grpcErrorResponse(status));
											 return;
										 }
									 Json::Value ret;
									 ret["upload_id"] = response->upload_id();
									 ret["object_key"] = response->object_key();
									 ret["part_size"] = Json::Int64(response->part_size());
									 ret["total_parts"] = response->total_parts();
									 ret["status"] = response->status();
									 ret["message"] = response->message();
									 auto httpStatus = response->status() == "infected" ? k409Conflict : k200OK;
									 LOG_INFO_RID(rid, "[Initupload] user:{} init {} status={}",
												  request->user_id(), request->file_name(), response->status());
									 callback(makeJsonResponse(ret, httpStatus));
								 });
}

void FileController::PresignParts(const HttpRequestPtr &req,
								  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
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

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id") || !(*jsonPtr)["part_numbers"].isArray())
	{
		callback(makeErrorResponse("invalid_json", "missing upload_id/part_numbers", k400BadRequest));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::PresignPartsReq>();
	auto response = std::make_shared<::file::PresignPartsResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());
	for (const auto &pn : (*jsonPtr)["part_numbers"])
	{
		request->add_part_numbers(pn.asInt());
		}
		context->AddMetadata("x-request-id", rid);
		context->AddMetadata("x-user-id", std::to_string(userId));

		stub->async()->PresignParts(context.get(), request.get(), response.get(),
									[context, request, response, callback](::grpc::Status status) {
										if (response == nullptr)
										{
											callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
											return;
										}
										if (!status.ok())
										{
											callback(grpcErrorResponse(status));
											return;
										}
									Json::Value ret;
									ret["upload_id"] = response->upload_id();
									ret["parts"] = Json::Value(Json::arrayValue);
									for (int i = 0; i < response->parts_size(); ++i)
									{
										const auto &part = response->parts(i);
										Json::Value item;
										item["part_number"] = part.part_number();
										item["url"] = part.url();
										item["expires_at"] = part.expires_at();
										ret["parts"].append(item);
									}
									callback(makeJsonResponse(ret, k200OK));
								});
}

void FileController::Uploadpart(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	const auto uploadUrl = req->getHeader("X-Presigned-Part-Url");
	if (uploadUrl.empty())
	{
		callback(makeErrorResponse("invalid_request", "missing X-Presigned-Part-Url", k400BadRequest));
		return;
	}

	PresignedUploadTarget target;
	std::string parseError;
	if (!parsePresignedUploadUrl(uploadUrl, target, parseError))
	{
		callback(makeErrorResponse("invalid_upload_url", parseError, k400BadRequest));
		return;
	}

	auto client = drogon::HttpClient::newHttpClient(target.hostString);
	auto upstreamReq = HttpRequest::newHttpRequest();
	upstreamReq->setMethod(Put);
	upstreamReq->setPathEncode(false);
	upstreamReq->setPath(target.path);
	for (const auto &[key, value] : target.queryParameters)
	{
		upstreamReq->setParameter(key, value);
	}
	upstreamReq->setBody(std::string(req->body()));
	if (const auto contentType = req->getHeader("Content-Type"); !contentType.empty())
	{
		upstreamReq->setCustomContentTypeString(contentType);
	}
	if (!rid.empty())
	{
		upstreamReq->addHeader("X-Request-Id", rid);
	}

	client->sendRequest(
		upstreamReq,
		[callback = std::move(callback), rid](ReqResult result, const HttpResponsePtr &resp) mutable {
			if (result != ReqResult::Ok || resp == nullptr)
			{
				callback(makeErrorResponse("upload_proxy_failed",
										   std::string(to_string_view(result)),
										   k502BadGateway));
				return;
			}

			if (resp->statusCode() < k200OK || resp->statusCode() >= k300MultipleChoices)
			{
				auto details = resp->body().empty() ? "upstream upload rejected"
													: std::string(resp->body());
				callback(makeErrorResponse("upload_proxy_failed", details, k502BadGateway));
				return;
			}

			Json::Value ret;
			ret["etag"] = resp->getHeader("ETag");
			LOG_INFO_RID(rid, "[uploadpart] proxied multipart upload");
			callback(makeJsonResponse(ret, k200OK));
		},
		120.0);
}

void FileController::CompleteMultipart(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

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

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::CompleteReq>();
	auto response = std::make_shared<::file::CompleteResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());
	context->AddMetadata("x-request-id", rid);
	context->AddMetadata("x-user-id", std::to_string(userId));

	stub->async()->CompleteMultipart(context.get(), request.get(), response.get(),
									 [context, request, response, callback](::grpc::Status status) {
										 if (response == nullptr)
										 {
											 callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
											 return;
										 }
										 if (!status.ok())
										 {
											 callback(grpcErrorResponse(status));
											 return;
										 }
										 Json::Value ret;
										 ret["object_key"] = response->object_key();
										 ret["etag"] = response->etag();
										 ret["status"] = response->status();
										 callback(makeJsonResponse(ret, k200OK));
									 });
}

void FileController::AbortMultipart(const HttpRequestPtr &req,
									std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

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

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::AbortReq>();
	auto response = std::make_shared<::file::AbortResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());
	context->AddMetadata("x-request-id", rid);
	context->AddMetadata("x-user-id", std::to_string(userId));

	stub->async()->AbortMultipart(context.get(), request.get(), response.get(),
								  [context, request, response, callback](::grpc::Status status) {
									  if (response == nullptr)
									  {
										  callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
										  return;
									  }
									  if (!status.ok())
									  {
										  callback(grpcErrorResponse(status));
										  return;
									  }
									  Json::Value ret;
									  ret["status"] = "aborted";
									  callback(makeJsonResponse(ret, k200OK));
								  });
}

void FileController::Status(const HttpRequestPtr &req,
							std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
		return;
	}

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

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::StatusReq>();
	auto response = std::make_shared<::file::StatusResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());
	context->AddMetadata("x-request-id", rid);
	context->AddMetadata("x-user-id", std::to_string(userId));

	stub->async()->Status(context.get(), request.get(), response.get(),
							  [context, request, response, callback](::grpc::Status status) {
								  if (response == nullptr)
								  {
									  callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
									  return;
								  }
								  if (!status.ok())
								  {
									  callback(grpcErrorResponse(status));
									  return;
								  }
							  Json::Value ret;
							  ret["total_parts"] = response->total_parts();
							  ret["uploaded_parts"] = Json::Value(Json::arrayValue);
							  for (int i = 0; i < response->uploaded_parts_size(); ++i)
							  {
								  ret["uploaded_parts"].append(response->uploaded_parts(i));
							  }
							  callback(makeJsonResponse(ret, k200OK));
						  });
}

void FileController::DeleteFile(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		callback(makeErrorResponse("service_unavailable", "", k503ServiceUnavailable));
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

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("filename") || !(*jsonPtr).isMember("filehash"))
	{
		callback(makeErrorResponse("invalid_json", "missing filename/filehash", k400BadRequest));
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::file::ReqDeleteFile>();
	auto response = std::make_shared<::file::Resp>();
	request->set_username(name);
	request->set_userid(std::to_string(userId));
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	context->AddMetadata("x-request-id", rid);

	stub->async()->DeleteFile(context.get(), request.get(), response.get(),
								  [context, request, response, callback](::grpc::Status status) {
									  if (response == nullptr)
									  {
										  callback(makeErrorResponse("grpc_error", "empty grpc response", k502BadGateway));
										  return;
									  }
									  if (!status.ok())
									  {
										  callback(grpcErrorResponse(status));
										  return;
									  }
								  Json::Value ret;
								  ret["status"] = response->code();
								  ret["message"] = response->message();
								  callback(makeJsonResponse(ret, response->code() == 0 ? k200OK : k409Conflict));
							  });
}
