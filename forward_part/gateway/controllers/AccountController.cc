#include "AccountController.h"

std::shared_ptr<account::accountService::Stub> AccountController::FindService(const std::string &key) const
{
	// 1. 从配置读取 Consul 地址
	std::string host = MyAppData::instance().consulHost;
	int port = MyAppData::instance().consulPort;

	// 2. 查询 Consul 获取负载均衡后的实例
	CloudiskConsul consul(host, port);
	ServiceInstance inst = consul.getRoundRobinInstance(key);

	if (inst.address.empty())
	{
		LOG_ERROR("[FindService] No available instance for {}", key);
		return nullptr;
	}

	LOG_INFO("[FindService] Use instance {}:{} for {}", inst.address, inst.port, key);

	// 3. 获取或创建 gRPC channel
	if (!inst.channel)
	{
		std::string addr = inst.address + ":" + std::to_string(inst.port);
		inst.channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
		// 注意：这里假设 getRoundRobinInstance 返回的是副本，所以需要重新 put 才能更新缓存中的 channel
		// 但 getRoundRobinInstance 逻辑可能比较复杂，如果它每次都返回新对象，我们需要确保能更新到缓存
	}

	// 4. 创建并返回 stub
	return account::accountService::NewStub(inst.channel);
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
							  drogon::app().getLoop()->queueInLoop([callback, context, request, response, s]()
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
								  ret["error"] = "Internal Service Error";
								  ret["details"] = s.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(k500InternalServerError);
								  callback(resp);
							  } });
						  });
}

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
							  drogon::app().getLoop()->queueInLoop([callback, context, request, response, s]()
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
								  ret["error"] = "Internal Service Error";
								  ret["details"] = s.error_message();
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  resp->setStatusCode(k500InternalServerError);
								  callback(resp);
							  } });
						  });
}
