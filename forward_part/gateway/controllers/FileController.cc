#include "FileController.h"
#include "Hash.h"

static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/false);

	return state == GRPC_CHANNEL_READY;
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
	ServiceInstance value;

	// 1. 尝试从缓存取
	if (cache_.get(key, value))
	{
		if (value.channel && isChannelReady(value.channel))
		{
			LOG_INFO("[ARC] HIT key = {}, addr = {}:{}", key, value.address, value.port);
			return value.file_stub; // ✔缓存有效
		}
		LOG_INFO("[ARC] MISS key = {}", key);
	}
	// 2. 未命中缓存 → 去 Consul 查询
	std::string host = MyAppData::instance().consulHost;
	int port = MyAppData::instance().consulPort;
	CloudiskConsul consul(host, port);
	value = consul.getRoundRobinInstance(key);

	if (value.address.empty())
	{
		LOG_ERROR("[FindService] No available instance for {}", key);
		return nullptr;
	}
	LOG_INFO("[FindService] Use instance {}:{} for {}", value.address, value.port, key);

	// 3. 为当前 address:port 创建唯一的 channel + stub，并放缓存
	std::string addr = value.address + ":" + std::to_string(value.port);
	value.channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
	value.file_stub = file::fileService::NewStub(value.channel);
	cache_.put(key, value); // ← 必须写回缓存

	return value.file_stub;
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
								 [context, request, response, callback](::grpc::Status status)
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
										 LOG_INFO("[filequeryinfo] user:{}   find {} files", request->username(), response->files_size());
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										 resp->setStatusCode(k200OK);
										 callback(resp);
									 }
									 else
									 {
										 LOG_INFO("[filequeryinfo] user:{}   find {} files", request->username(), response->files_size());
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
		LOG_ERROR("[filedowm] invalid JSON in request");
		return;
	}
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	stub->async()->filedowm(context.get(), request.get(), response.get(),
							[context, request, response, callback](::grpc::Status status)
							{
								if (!status.ok() || response == nullptr)
								{
									LOG_INFO("[filedowm] user:{} find {} download file failed",
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
								auto resp = drogon::HttpResponse::newHttpResponse();
								resp->setStatusCode(drogon::k302Found);
								resp->addHeader("Location", downloadURL);
								callback(resp);
								LOG_INFO("[filedowm] user:{} find {} download file oss signed url {}",
										 request->username(), request->filename(), downloadURL);
							});
}

void FileController::LoadFile(const HttpRequestPtr &req,
							  std::function<void(const HttpResponsePtr &)> &&callback) const
{
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
							[context, request, response, callback](::grpc::Status status)
							{
								if (status.ok() && response->code() == 0)
								{
									Json::Value ret;
									ret["status"] = response->code();
									ret["message"] = response->message();
									LOG_INFO("[LoadFile] user:{}   find {} Load ", request->username(), request->filename());
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(k200OK);
									callback(resp);
								}
								else
								{
									LOG_INFO("[LoadFile] user:{}   find {} Load file", request->username(), request->filename());
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
		LOG_ERROR("[Showfile] invalid JSON in request");
		callback(resp);
		return;
	}
	request->set_filename((*jsonPtr)["filename"].asString());
	request->set_filehash((*jsonPtr)["filehash"].asString());
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	request->set_file_size((*jsonPtr)["file_size"].asInt64());
	stub->async()->Showfile(context.get(), request.get(), response.get(),
							[context, request, response, callback](::grpc::Status status)
							{
								if (!status.ok() || response == nullptr)
								{
									LOG_INFO("[Showfile] user:{} find {} show failed",
											 request->username(), request->filename());

									Json::Value ret;
									ret["error"] = "grpc_error";
									ret["details"] = status.error_message();
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									resp->setStatusCode(drogon::k500InternalServerError);
									callback(resp);
									LOG_INFO("[Showfile] user:{} find {} show file failed",
											 request->username(), request->filename());
									return;
								}

								std::string previewURL;
								// response->message() 是可直接打开的 inline signed url
								previewURL = response->message();

								auto resp = drogon::HttpResponse::newHttpResponse();
								resp->setStatusCode(drogon::k303SeeOther);
								resp->addHeader("Location", previewURL);
								callback(resp);
								LOG_INFO("[Showfile] user:{} find {} show file oss signed url url{}",
										 request->username(), request->filename(), previewURL);
							});
}