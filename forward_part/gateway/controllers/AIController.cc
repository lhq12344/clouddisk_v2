#include "AIController.h"
#include <json/json.h>

// 解析 JSON 字符串
static bool parseJsonString(const std::string &s, Json::Value &out, std::string &errs)
{
	Json::CharReaderBuilder b;
	std::unique_ptr<Json::CharReader> reader(b.newCharReader());
	return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
}

// 声明外部函数，避免重复定义
extern bool getArgumentsFromJWT(const HttpRequestPtr &req, drogon::HttpResponsePtr &resp, std::string &name, int &userId);

static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/false);

	return state == GRPC_CHANNEL_READY;
}

std::shared_ptr<AI::AIService::Stub> AIController::FindService(const std::string &key) const
{
	ServiceInstance value;

	// 1. 尝试从缓存取
	if (cache_.get(key, value))
	{
		if (value.channel && isChannelReady(value.channel))
		{
			LOG_INFO("[ARC] HIT key = {}, addr = {}:{}", key, value.address, value.port);
			return value.AI_stub; // ✔缓存有效
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
	value.AI_stub = AI::AIService::NewStub(value.channel);
	cache_.put(key, value); // ← 必须写回缓存

	return value.AI_stub;
}

void AIController::aiRequest(const HttpRequestPtr &req,
							 std::function<void(const HttpResponsePtr &)> &&callback)
{
	// 1) 查找 AI 服务实例
	auto stub = FindService("AI_srv");
	if (!stub)
	{
		Json::Value ret;
		ret["error"] = "no_ai_service";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(drogon::k500InternalServerError);
		callback(resp);
		return;
	}

	// 2) JWT 解析得到用户身份（你已有）
	std::string username;
	int userId = 0;
	drogon::HttpResponsePtr jwtFailResp;
	if (!getArgumentsFromJWT(req, jwtFailResp, username, userId))
	{
		callback(jwtFailResp);
		return;
	}
	// 关键修复：强校验，禁止 0/空字符串继续走
	if (userId <= 0 || username.empty())
	{
		Json::Value ret;
		ret["error"] = "unauthorized";
		ret["message"] = "invalid jwt claims (missing user id / username)";
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(drogon::k401Unauthorized);
		callback(resp);
		return;
	}
	// 3) 解析用户自然语言 query + 可选文件上下文
	std::string query;
	std::string filename;
	std::string filehash;
	int32_t fileSize = 0;

	// 3.1 优先从 JSON body 取
	if (req->contentType() == drogon::CT_APPLICATION_JSON)
	{
		auto json = req->getJsonObject();
		if (json)
		{
			if ((*json).isMember("query"))
				query = (*json)["query"].asString();
			if ((*json).isMember("filename"))
				filename = (*json)["filename"].asString();
			if ((*json).isMember("filehash"))
				filehash = (*json)["filehash"].asString();
			if ((*json).isMember("file_size"))
				fileSize = (int32_t)(*json)["file_size"].asInt();
		}
	}

	// 3.2 其次从 query 参数取（GET /ai?q=...）
	if (query.empty())
		query = req->getParameter("q");
	if (filename.empty())
		filename = req->getParameter("filename");
	if (filehash.empty())
		filehash = req->getParameter("filehash");
	if (fileSize == 0)
	{
		auto fs = req->getParameter("file_size");
		if (!fs.empty())
		{
			try
			{
				fileSize = (int32_t)std::stol(fs);
			}
			catch (...)
			{
			}
		}
	}

	// 3.3 若仍没有 query，给一个默认值（避免 LLM 无输入）
	if (query.empty())
	{
		query = "请根据当前用户上下文进行帮助。";
	}

	// 4) 构造 gRPC 请求
	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::AI::AIReq>();
	auto response = std::make_shared<::AI::AIResp>();

	request->set_userid(std::to_string(userId));
	request->set_username(username);
	request->set_query(query);

	if (!filename.empty())
		request->set_filename(filename);
	if (!filehash.empty())
		request->set_filehash(filehash);
	if (fileSize > 0)
		request->set_file_size(fileSize);

	// 5) 发起异步 gRPC
	stub->async()->AIrequest(context.get(), request.get(), response.get(),
							 [context, request, response, callback](::grpc::Status status)
							 {
								 // 5.1 gRPC 层失败
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

								 // 5.2 业务层失败（AIResp.code != 200）
								 if (response->code() != 200)
								 {
									 Json::Value ret;
									 ret["error"] = "ai_error";
									 ret["code"] = response->code();
									 ret["message"] = response->message();
									 ret["data"] = response->data();
									 auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									 resp->setStatusCode(drogon::k400BadRequest);
									 callback(resp);
									 return;
								 }

								 // 5.3 成功：优先解析 data 为 JSON action
								 const std::string data = response->data().empty() ? response->message() : response->data();

								 Json::Value out;
								 std::string errs;
								 if (!data.empty() && parseJsonString(data, out, errs) && out.isObject())
								 {
									 const std::string action = out.get("action", "text").asString();

									 if (action == "redirect")
									 {
										 const std::string url = out.get("url", "").asString();
										 auto resp = drogon::HttpResponse::newHttpResponse();
										 resp->setStatusCode(drogon::k302Found);
										 resp->addHeader("Location", url);
										 callback(resp);
										 return;
									 }

									 if (action == "json")
									 {
										 // payload 必须是对象/数组
										 auto resp = drogon::HttpResponse::newHttpJsonResponse(out["payload"]);
										 resp->setStatusCode(drogon::k200OK);
										 callback(resp);
										 return;
									 }

									 // 默认 text
									 const std::string text = out.get("text", "").asString();
									 auto resp = drogon::HttpResponse::newHttpResponse();
									 resp->setStatusCode(drogon::k200OK);
									 resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
									 resp->setBody(text);
									 callback(resp);
									 return;
								 }

								 // 5.4 data 不是 JSON：按纯文本返回（更鲁棒）
								 auto resp = drogon::HttpResponse::newHttpResponse();
								 resp->setStatusCode(drogon::k200OK);
								 resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
								 resp->setBody(data);
								 callback(resp);
							 });
}
