#include "AIController.h"

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

void AIController::aiRequest(const HttpRequestPtr &req,
							 std::function<void(const HttpResponsePtr &)> &&callback)
{
	// 1. 查找 AI 服务实例
	auto stub = FindService("ai_srv");
	if (!stub)
	{
		auto resp = HttpResponse::newHttpResponse();
		resp->setStatusCode(k500InternalServerError);
		resp->setContentTypeCode(CT_TEXT_PLAIN);
		resp->setBody("No available accountService instance");
		callback(resp);
		return;
	}

	// 2. 构造 gRPC 请求
	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::AI::AIReq>();
	auto response = std::make_shared<::AI::AIResp>();

	std::string name;
	int userId = 0;
	drogon::HttpResponsePtr resp;
	if (!getArgumentsFromJWT(req, resp, name, userId))
	{
		callback(resp);
		return;
	}

	// 3. 处理 gRPC 响应
	request->set_userid(std::to_string(userId));
	request->set_username(name);
	stub->async()->AIrequest(context.get(), request.get(), response.get(),
							 [context, request, response, callback](::grpc::Status status)
							 {
								 if (!status.ok() || response == nullptr)
								 {
									 LOG_INFO("[aiRequest] user:{} file {} request failed",
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
								 LOG_INFO("[aiRequest] user:{} file {} request oss signed url {}",
										  request->username(), request->filename(), downloadURL);
							 });
}
