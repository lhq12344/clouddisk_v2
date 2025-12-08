#include "AccountController.h"

bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/false);

	return state == GRPC_CHANNEL_READY;
}

std::shared_ptr<account::accountService::Stub>
AccountController::FindService(const std::string &key) const
{
	ServiceInstance value;

	// 1. 尝试从缓存取
	if (cache_.get(key, value))
	{
		if (value.channel && isChannelReady(value.channel))
		{
			LOG_INFO("[ARC] HIT key = {}, addr = {}:{}", key, value.address, value.port);
			return value.stub; // ✔缓存有效
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
	value.stub = account::accountService::NewStub(value.channel);
	cache_.put(key, value); // ← 必须写回缓存

	return value.stub;
}

void AccountController::signup(
	const drogon::HttpRequestPtr &req,
	std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	auto stub = FindService("account_srv");

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::account::ReqSignup>();
	auto response = std::make_shared<::account::Resp>();

	// 获取请求参数 (支持 JSON)
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		LOG_ERROR("[signup] Failed to get JSON object from request");
		return;
	}
	request->set_username((*jsonPtr)["username"].asString());
	request->set_password((*jsonPtr)["password"].asString());
	// 发起异步调用，捕获所有 shared_ptr 以延长生命周期
	// 注意：std::function 要求 lambda 是可复制的，因此不能捕获 unique_ptr (即使是 move)。
	// 必须使用 shared_ptr 来管理 stub。
	stub->async()->Signup(context.get(), request.get(), response.get(),
						  [callback, context, request, response](::grpc::Status s)
						  {
							  if (s.ok() && response->code() == 0)
							  {
								  Json::Value ret;
								  ret["status"] = "ok";
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  callback(resp);
								  LOG_INFO("[signup] user:{}   user registering", request->username());
							  }
							  else
							  {
								  LOG_ERROR("[signup] gRPC Signup failed: {} {}", (int)s.error_code(), s.error_message());
								  Json::Value ret;
								  ret["error"] = s.error_code();
								  ret["details"] = s.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(k500InternalServerError);
								  callback(resp);
							  } });
};
void AccountController::signin(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
	auto stub = FindService("account_srv");

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::account::ReqSignin>();
	auto response = std::make_shared<::account::Resp>();

	// 获取请求参数 (支持 JSON)
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr)
	{
		LOG_ERROR("[signup] Failed to get JSON object from request");
		return;
	}
	request->set_username((*jsonPtr)["username"].asString());
	request->set_password((*jsonPtr)["password"].asString());
	// 发起异步调用，捕获所有 shared_ptr 以延长生命周期
	// 注意：std::function 要求 lambda 是可复制的，因此不能捕获 unique_ptr (即使是 move)。
	// 必须使用 shared_ptr 来管理 stub。
	stub->async()->Signin(context.get(), request.get(), response.get(),
						  [callback, context, request, response](::grpc::Status s)
						  {
							  if (s.ok() && response->code() == 0)
							  {
								  Json::Value ret;
								  ret["status"] = "ok";
								  ret["token"] = response->message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  callback(resp);
								  LOG_INFO("[signin] user:{}   user registering", request->username());
							  }
							  else
							  {
								  LOG_ERROR("[signin] gRPC Signin failed: {} {}", (int)s.error_code(), s.error_message());
								  Json::Value ret;
								  ret["error"] = s.error_code();
								  ret["details"] = s.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(k500InternalServerError);
								  callback(resp);
							  } });
}

void AccountController::userinfo(const HttpRequestPtr &req,
								 std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto stub = FindService("account_srv");

	auto context = std::make_shared<::grpc::ClientContext>();
	auto request = std::make_shared<::account::ReqUserinfo>();
	auto response = std::make_shared<::account::Resp>();

	request->set_username(req->getAttributes()->get<std::string>("Name"));
	request->set_id(req->getAttributes()->get<int>("ID"));
	stub->async()->Userinfo(context.get(), request.get(), response.get(),
						  [callback, context, request, response](::grpc::Status s)
						  {
							  if (s.ok() && response->code() == 0)
							  {
								  Json::Value ret;
								  ret["status"] = "ok";
								  ret["token"] = response->message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  callback(resp);
								 
							  }
							  else
							  {
								  LOG_ERROR("[signin] gRPC Signin failed: {} {}", (int)s.error_code(), s.error_message());
								  Json::Value ret;
								  ret["error"] = s.error_code();
								  ret["details"] = s.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(k500InternalServerError);
								  callback(resp);
							  } });
}