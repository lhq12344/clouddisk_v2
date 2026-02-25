#include "FileController.h"
#include "Hash.h"

#include <cctype>

class UploadPartReactor final : public grpc::ClientWriteReactor<file::UploadPartReq>
{
public:
	UploadPartReactor(std::shared_ptr<file::fileService::Stub> stub,
					  const drogon::HttpRequestPtr &req,
					  file::UploadPartMeta meta,
					  std::string requestId,
					  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
		: stub_(std::move(stub)), req_(req), meta_(std::move(meta)), rid_(std::move(requestId)), callback_(std::move(callback))
	{
		if (!rid_.empty())
		{
			context_.AddMetadata("x-request-id", rid_);
		}
		stub_->async()->UploadPart(&context_, &response_, this);
		StartCall();
		request_.mutable_meta()->Swap(&meta_);
		StartWrite(&request_);
	}

	void OnWriteDone(bool ok) override
	{
		if (!ok)
		{
			context_.TryCancel();
			return;
		}
		if (stage_ == 0)
		{
			stage_ = 1;
			request_.Clear();
			request_.set_data(req_->getBody().data(), req_->getBody().size());
			StartWrite(&request_);
			return;
		}
		if (stage_ == 1)
		{
			stage_ = 2;
			StartWritesDone();
			return;
		}
	}

	void OnDone(const grpc::Status &status) override
	{
		if (!status.ok())
		{
			Json::Value ret;
			ret["error"] = "grpc_error";
			ret["details"] = status.error_message();
			auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
			resp->setStatusCode(drogon::k500InternalServerError);
			callback_(resp);
			delete this;
			return;
		}
		Json::Value ret;
		ret["etag"] = response_.etag();
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(drogon::k200OK);
		callback_(resp);
		delete this;
	}

private:
	std::shared_ptr<file::fileService::Stub> stub_;
	grpc::ClientContext context_;
	file::UploadPartMeta meta_;
	drogon::HttpRequestPtr req_;
	file::UploadPartReq request_;
	file::UploadPartResp response_;
	int stage_{0};
	std::string rid_;
	std::function<void(const drogon::HttpResponsePtr &)> callback_;
};

static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/true);

	return state == GRPC_CHANNEL_READY ||
		   state == GRPC_CHANNEL_IDLE ||
		   state == GRPC_CHANNEL_CONNECTING;
}
static std::string url_encode(const std::string &value)
{
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex << std::uppercase;
	for (unsigned char c : value)
	{
		// 保留 unreserved characters
		if ((c >= '0' && c <= '9') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			c == '-' || c == '_' || c == '.' || c == '~')
		{
			escaped << c;
		}
		else
		{
			escaped << '%' << std::setw(2) << int(c);
		}
	}
	return escaped.str();
}

std::shared_ptr<file::fileService::Stub> FileController::FindService(const std::string &key) const
{
	CloudiskConsul consul(MyAppData::instance().consulHost, MyAppData::instance().consulPort);

	return ArcGrpcLB::FindService<file::fileService>(
		cache_, consul, key, 10,
		[](const std::shared_ptr<grpc::Channel> &ch)
		{ return isChannelReady(ch); });
}

bool getArgumentsFromJWT(const HttpRequestPtr &req, drogon::HttpResponsePtr &resp, std::string &name, int &userId)
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

void FileController::filequeryinfo(const HttpRequestPtr &req,
								   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::ReqFileQuery>();
	auto response = std::make_shared<::file::RespFileQuery>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}
	// 关键修复：强校验，禁止 0/空字符串继续走
	if (userId <= 0 || name.empty())
	{
		Json::Value ret;
		ret["error"] = "unauthorized";
		ret["message"] = "invalid jwt claims (missing user id / username)";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(drogon::k401Unauthorized);
		callback(resp);
		return;
	}
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	stub->async()->filequeryinfo(context.get(), request.get(), response.get(),
								 [context, request, response, callback, rid](::grpc::Status status)
								 {
									 if (status.ok() && response->code() == 0)
									 {
										 Json::Value ret;
										 ret["status"] = response->code();
										 ret["message"] = response->message();
										 ret["filelist"] = Json::Value(Json::arrayValue);
										 for (int i = 0; i < response->files_size(); ++i)
										 {
											 const auto &fileinfo = response->files(i);
											 Json::Value fileJson;
											 fileJson["filename"] = fileinfo.file_name();
											 fileJson["filesize"] = fileinfo.file_sizes();
											 fileJson["filehash"] = fileinfo.file_hash();
											 ret["filelist"].append(fileJson);
										 }
										 LOG_INFO_RID(rid, "[filequeryinfo] user:{}   find {} files", request->username(), response->files_size());
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										 resp->setStatusCode(k200OK);
										 callback(resp);
									 }
									 else
									 {
										 LOG_INFO_RID(rid, "[filequeryinfo] user:{}   find {} files", request->username(), response->files_size());
										 Json::Value ret;
										 ret["error"] = "grpc_error";
										 ret["details"] = status.error_message();
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										 resp->setStatusCode(k500InternalServerError);
										 callback(resp);
									 }
								 });
}
void FileController::filedowm(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";

		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::ReqFileDown>();
	auto response = std::make_shared<::file::Resp>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		callback(resp);
		LOG_ERROR_RID(rid, "[filedowm] invalid JSON in request");
		return;
	}
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	stub->async()->filedowm(context.get(), request.get(), response.get(),
							[context, request, response, callback, rid](::grpc::Status status)
							{
								if (!status.ok() || response == nullptr)
								{
									LOG_INFO_RID(rid, "[filedowm] user:{} find {} download file failed",
											 request->username(), request->filename());

									Json::Value ret;
									ret["error"] = "grpc_error";
									ret["details"] = status.error_message();
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(drogon::k500InternalServerError);
									callback(resp);
									return;
								}

								std::string downloadURL;
								downloadURL = response->message(); // 这里已经是完整 signed URL

								Json::Value ret;
								ret["download_url"] = downloadURL;
								ret["filename"] = request->filename();
								auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								resp->setStatusCode(drogon::k200OK);
								callback(resp);
								LOG_INFO_RID(rid, "[filedowm] user:{} find {} download file oss signed url {}",
										 request->username(), request->filename(), downloadURL);
							});
}

void FileController::LoadFile(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::Reqloadfile>();
	auto response = std::make_shared<::file::Resp>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		callback(resp);
		return;
	}

	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_content((*jsonPtr)["content"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["content"].asString().size());
	request->set_file_hash(Hash((*jsonPtr)["filename"].asString(), (*jsonPtr)["content"].asString()).sha256());
	stub->async()->LoadFile(context.get(), request.get(), response.get(),
							[context, request, response, callback, rid](::grpc::Status status)
							{
								if (status.ok() && response->code() == 0)
								{
									Json::Value ret;
									ret["status"] = response->code();
									ret["message"] = response->message();
									LOG_INFO_RID(rid, "[LoadFile] user:{}   find {} Load ", request->username(), request->filename());
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(k200OK);
									callback(resp);
								}
								else
								{
									LOG_INFO_RID(rid, "[LoadFile] user:{}   find {} Load file", request->username(), request->filename());
									Json::Value ret;
									ret["error"] = "grpc_error";
									ret["details"] = status.error_message();
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(k500InternalServerError);
									callback(resp);
								}
							});
}

void FileController::Showfile(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::Reqshowfile>();
	auto response = std::make_shared<::file::Resp>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		LOG_ERROR_RID(rid, "[Showfile] invalid JSON in request");
		callback(resp);
		return;
	}
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	stub->async()->Showfile(context.get(), request.get(), response.get(),
							[context, request, response, callback, rid](::grpc::Status status)
							{
								if (!status.ok() || response == nullptr)
								{
									LOG_INFO_RID(rid, "[Showfile] user:{} find {} show failed",
											 request->username(), request->filename());

									Json::Value ret;
									ret["error"] = "grpc_error";
									ret["details"] = status.error_message();
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(drogon::k500InternalServerError);
									callback(resp);
									return;
								}

								std::string previewURL;
								// response->message() 是可直接打开的 inline signed url
								previewURL = response->message();

								Json::Value ret;
								ret["preview_url"] = previewURL;
								ret["filename"] = request->filename();
								auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								resp->setStatusCode(drogon::k200OK);
								callback(resp);
								LOG_INFO_RID(rid, "[Showfile] user:{} find {} show file oss signed url url{}",
										 request->username(), request->filename(), previewURL);
							});
}

void FileController::Initupload(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::InitReq>();
	auto response = std::make_shared<::file::InitResp>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		LOG_ERROR_RID(rid, "[Initupload] invalid JSON in request");
		callback(resp);
		return;
	}
	request->set_file_name((*jsonPtr)["file_name"].asString());
	request->set_file_hash((*jsonPtr)["file_hash"].asString());
	request->set_user_id(std::to_string(userId));
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	if ((*jsonPtr).isMember("content_type"))
		request->set_content_type((*jsonPtr)["content_type"].asString());
	stub->async()->InitMultipart(context.get(), request.get(), response.get(),
								 [context, request, response, callback, rid](::grpc::Status status)
								 {
									 if (!status.ok() || response == nullptr)
									 {
										 LOG_INFO_RID(rid, "[Initupload] userid:{} find {} init failed",
												  request->user_id(), request->file_name());

										 Json::Value ret;
										 ret["error"] = "grpc_error";
										 ret["details"] = status.error_message();
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										 resp->setStatusCode(drogon::k500InternalServerError);
										 callback(resp);
										 return;
									 }

									 Json::Value ret;
									 ret["upload_id"] = response->upload_id();
									 ret["object_key"] = response->object_key();
									 ret["part_size"] = response->part_size();
									 ret["total_parts"] = response->total_parts();
									 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									 resp->setStatusCode(drogon::k200OK);
									 callback(resp);
									 LOG_INFO_RID(rid, "[Initupload] user:{} start load find {} ",
											  request->user_id(), request->file_name());
								 });
}

// 小工具：安全转整数
static bool parseInt(const std::string &s, int &out)
{
	if (s.empty())
		return false;
	char *end = nullptr;
	long v = std::strtol(s.c_str(), &end, 10);
	if (!end || *end != '\0')
		return false;
	if (v <= 0 || v > INT32_MAX)
		return false;
	out = static_cast<int>(v);
	return true;
}

void FileController::Uploadpart(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	// 1) 发现服务
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
		r->setStatusCode(drogon::k503ServiceUnavailable);
		callback(r);
		return;
	}

	// 2) 鉴权
	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr authResp;
	if (!getArgumentsFromJWT(req, authResp, name, userId))
	{
		callback(authResp);
		return;
	}

	// 3) 统一从 Header 读取 meta
	//    也可以改成从 URL path 取 upload_id/part_number，这里先按你现状走 header
	const std::string uploadId = req->getHeader("X-Upload-Id");
	const std::string partNoStr = req->getHeader("X-Part-Number");
	const std::string chunkHash = req->getHeader("X-Chunk-Hash");  // optional
	const std::string partSizeStr = req->getHeader("X-Part-Size"); // optional

	int partNumber = 0;
	if (uploadId.empty() || !parseInt(partNoStr, partNumber))
	{
		Json::Value ret;
		ret["error"] = "invalid_request";
		ret["details"] = "missing/invalid X-Upload-Id or X-Part-Number";
		auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
		r->setStatusCode(drogon::k400BadRequest);
		callback(r);
		return;
	}

	// 4) 读取 body（二进制分片数据）
	// Drogon 的 request body 直接从 getBody/body 获取即可 :contentReference[oaicite:1]{index=1}
	std::string_view body = req->getBody();
	if (body.empty())
	{
		Json::Value ret;
		ret["error"] = "invalid_request";
		ret["details"] = "empty body";
		auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
		r->setStatusCode(drogon::k400BadRequest);
		callback(r);
		return;
	}

	int64_t partSize = static_cast<int64_t>(body.size());
	if (!partSizeStr.empty())
	{
		// 前端传 part_size，就做一致性校验
		char *end = nullptr;
		long long ps = std::strtoll(partSizeStr.c_str(), &end, 10);
		if (!end || *end != '\0' || ps <= 0)
		{
			Json::Value ret;
			ret["error"] = "invalid_request";
			ret["details"] = "invalid X-Part-Size";
			auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
			r->setStatusCode(drogon::k400BadRequest);
			callback(r);
			return;
		}
		if (ps != partSize)
		{
			Json::Value ret;
			ret["error"] = "invalid_request";
			ret["details"] = "X-Part-Size != actual body size";
			auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
			r->setStatusCode(drogon::k400BadRequest);
			callback(r);
			return;
		}
		partSize = ps;
	}

	// 5) 组装 gRPC meta
	file::UploadPartMeta meta;
	meta.set_upload_id(uploadId);
	meta.set_part_number(partNumber);
	meta.set_part_size(partSize);
	if (!chunkHash.empty())
		meta.set_chunk_hash(chunkHash);

	// 6) 启动 client-stream 转发
	new UploadPartReactor(stub, req, std::move(meta), rid, std::move(callback));
}

void FileController::CompleteMultipart(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		ret["details"] = "missing upload_id";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::CompleteReq>();
	auto response = std::make_shared<::file::CompleteResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());

	stub->async()->CompleteMultipart(context.get(), request.get(), response.get(),
									 [context, request, response, callback](::grpc::Status status)
									 {
										 if (!status.ok() || response == nullptr)
										 {
											 Json::Value ret;
											 ret["error"] = "grpc_error";
											 ret["details"] = status.error_message();
											 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
											 resp->setStatusCode(drogon::k500InternalServerError);
											 callback(resp);
											 return;
										 }
										 Json::Value ret;
										 ret["object_key"] = response->object_key();
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										 resp->setStatusCode(drogon::k200OK);
										 callback(resp);
									 });
}

void FileController::AbortMultipart(const HttpRequestPtr &req,
									std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		ret["details"] = "missing upload_id";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::AbortReq>();
	auto response = std::make_shared<::file::AbortResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());

	stub->async()->AbortMultipart(context.get(), request.get(), response.get(),
								  [context, request, response, callback](::grpc::Status status)
								  {
									  if (!status.ok() || response == nullptr)
									  {
										  Json::Value ret;
										  ret["error"] = "grpc_error";
										  ret["details"] = status.error_message();
										  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										  resp->setStatusCode(drogon::k500InternalServerError);
										  callback(resp);
										  return;
									  }
									  Json::Value ret;
									  ret["status"] = "aborted";
									  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									  resp->setStatusCode(drogon::k200OK);
									  callback(resp);
								  });
}

void FileController::Status(const HttpRequestPtr &req,
							std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !(*jsonPtr).isMember("upload_id"))
	{
		Json::Value ret;
		ret["error"] = "invalid_json";
		ret["details"] = "missing upload_id";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k400BadRequest);
		callback(resp);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::StatusReq>();
	auto response = std::make_shared<::file::StatusResp>();
	request->set_upload_id((*jsonPtr)["upload_id"].asString());

	stub->async()->Status(context.get(), request.get(), response.get(),
						  [context, request, response, callback](::grpc::Status status)
						  {
							  if (!status.ok() || response == nullptr)
							  {
								  Json::Value ret;
								  ret["error"] = "grpc_error";
								  ret["details"] = status.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(drogon::k500InternalServerError);
								  callback(resp);
								  return;
							  }
							  Json::Value ret;
							  ret["total_parts"] = response->total_parts();
							  ret["uploaded_parts"] = Json::Value(Json::arrayValue);
							  for (int i = 0; i < response->uploaded_parts_size(); ++i)
							  {
								  ret["uploaded_parts"].append(response->uploaded_parts(i));
							  }
							  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
							  resp->setStatusCode(drogon::k200OK);
							  callback(resp);
						  });
}
void FileController::DeleteFile(const HttpRequestPtr &req,
								std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto stub = FindService("file_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "service_unavailable";
		auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
		r->setStatusCode(drogon::k503ServiceUnavailable);
		callback(r);
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
		Json::Value ret;
		ret["error"] = "invalid_json";
		ret["details"] = "missing filename/filehash";
		auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
		r->setStatusCode(drogon::k400BadRequest);
		callback(r);
		return;
	}

	auto context = std::make_shared<::grpc::ClientContext>();
	if (!rid.empty())
		context->AddMetadata("x-request-id", rid);
	auto request = std::make_shared<::file::ReqDeleteFile>();
	auto response = std::make_shared<::file::Resp>();

	request->set_username(name);
	request->set_userid(std::to_string(userId));
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());

	stub->async()->DeleteFile(context.get(), request.get(), response.get(),
							  [context, request, response, callback](::grpc::Status status)
							  {
								  if (status.ok() && response->code() == 0)
								  {
									  Json::Value ret;
									  ret["status"] = response->code();
									  ret["message"] = response->message();
									  auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
									  r->setStatusCode(drogon::k200OK);
									  callback(r);
								  }
								  else
								  {
									  Json::Value ret;
									  ret["error"] = "grpc_error";
									  ret["details"] = status.error_message();
									  auto r = drogon::HttpResponse::newHttpJsonResponse(ret);
									  r->setStatusCode(drogon::k500InternalServerError);
									  callback(r);
								  }
							  });
}