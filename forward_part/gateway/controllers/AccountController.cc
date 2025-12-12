#include "AccountController.h"
#include "../../../other_srv/email_srv/KafkaProducer.h"

drogon::HttpResponsePtr transError(const std::string &status, const std::string &msg, HttpStatusCode code)
{
	Json::Value ret;
	ret["state"] = status;
	ret["details"] = msg;

	auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
	resp->setStatusCode(code);
	return resp;
}

static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/false);

	return state == GRPC_CHANNEL_READY;
}

std::shared_ptr<account::accountService::Stub> AccountController::FindService(const std::string &key) const
{
	ServiceInstance value;

	// 1. 尝试从缓存取
	if (cache_.get(key, value))
	{
		if (value.channel && isChannelReady(value.channel))
		{
			LOG_INFO("[ARC] HIT key = {}, addr = {}:{}", key, value.address, value.port);
			return value.account_stub; // ✔缓存有效
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
	value.account_stub = account::accountService::NewStub(value.channel);
	cache_.put(key, value); // ← 必须写回缓存

	return value.account_stub;
}

void AccountController::signup(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	auto stub = FindService("account_srv");

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
	auto request = std::make_shared<::account::ReqSignup>();
	auto response = std::make_shared<::account::Resp>();

	// 获取请求参数 (支持 JSON)
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
	request->set_username((*jsonPtr)["username"].asString());
	request->set_password((*jsonPtr)["password"].asString());
	request->set_email((*jsonPtr)["email"].asString());
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
	auto request = std::make_shared<::account::ReqSignin>();
	auto response = std::make_shared<::account::Resp>();

	// 获取请求参数 (支持 JSON)
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
	auto request = std::make_shared<::account::ReqUserinfo>();
	auto response = std::make_shared<::account::Resp>();

	std::string name;
	int userId = 0;
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
		auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
		resp->setStatusCode(k401Unauthorized);
		callback(resp);
		return;
	}

	request->set_username(name);
	request->set_id(userId);
	stub->async()->Userinfo(context.get(), request.get(), response.get(),
							[callback, context, request, response](::grpc::Status s)
							{
							if (s.ok() && response->code() == 0)
							{
								Json::Value ret;
								ret["status"] = "ok";
								ret["message"] = response->message();
								auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								callback(resp);
								LOG_INFO("[signin] user:{}   user registering", request->username());
							}
							else{
								LOG_ERROR("[signin] gRPC Signin failed: {} {}", (int)s.error_code(), s.error_message());
								Json::Value ret;
								ret["error"] = s.error_code();
								ret["details"] = s.error_message();
								auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								resp->setStatusCode(k500InternalServerError);
								callback(resp);
							} });
}

void AccountController::sendcode(const HttpRequestPtr &req,
								 std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto json = req->getJsonObject();
	Json::Value ret;
	if (!json)
	{
		auto resp = transError("error", "Invalid JSON", k400BadRequest);
		return callback(resp);
	}

	std::string email = (*json)["email"].asString();
	if (email.empty())
	{
		auto resp = transError("error", "Email is empty", k400BadRequest);
		return callback(resp);
	}

	std::string host = MyAppData::instance().kafkaHost;
	int port = MyAppData::instance().kafkaPort;
	if (!KafkaProducer::instance().init(host + ":" + std::to_string(port)))
	{
		auto resp = transError("error", "Kafka producer init failed", k400BadRequest);
		return callback(resp);
	}
	std::string topic = "email_verify";
	if (!KafkaProducer::instance().send(topic, email))
	{
		auto resp = transError("error", "Kafka send failed", k400BadRequest);
		return callback(resp);
	}

	auto resp = transError("ok", "code send successfully", k200OK);
	callback(resp);
}

void AccountController::verifycode(const HttpRequestPtr &req,
								   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto json = req->getJsonObject();
	if (!json)
	{
		auto resp = transError("error", "Invalid JSON", k400BadRequest);
		callback(resp);
		return;
	}

	if (!json->isMember("email") || !json->isMember("code"))
	{
		auto resp = transError("error", "Missing email or code field", k400BadRequest);
		return callback(resp);
	}
	std::string email = (*json)["email"].asString();
	std::string code = (*json)["code"].asString();
	if (email.empty() || code.empty())
	{
		auto resp = transError("error", "Email or code is empty", k400BadRequest);
		return callback(resp);
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		auto resp = transError("error", "Redis client unavailable", k500InternalServerError);
		callback(resp);
		return;
	}
	redisClient->execCommandAsync(
		[code, callback](const drogon::nosql::RedisResult &r)
		{
			if (r.type() == nosql::RedisResultType::kNil)
			{
				LOG_INFO("Cannot find variable associated with the key 'email'");
				auto resp = transError("error", "Have no find email key", k400BadRequest);
				callback(resp);
				return;
			}
			else
			{
				if (r.asString() == code)
				{
					LOG_INFO("Name is {}", r.asString());
					auto resp = transError("ok", "Verification code matched", k200OK);
					callback(resp);
					return;
				}
				else
				{
					LOG_INFO("Verification code does not match");
					auto resp = transError("error", "Verification code does not match", k400BadRequest);
					callback(resp);
					return;
				}
			}
		},
		[callback](const std::exception &err)
		{
			LOG_ERROR("something failed!!! {}", err.what());
			auto resp = transError("error", "something failed!!!", k400BadRequest);
			callback(resp);
			return;
		},
		"get %s", email.c_str());
}