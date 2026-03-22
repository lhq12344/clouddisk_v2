#include "AccountController.h"
#include "GrpcHttp.h"
#include "../../../other_srv/email_srv/KafkaProducer.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <vector>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

drogon::HttpResponsePtr transError(const std::string &status, const std::string &msg, HttpStatusCode code)
{
	Json::Value ret;
	ret["state"] = status;
	ret["details"] = msg;

	auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
	resp->setStatusCode(code);
	return resp;
}

namespace
{
	const std::string kJwtBlacklistKey = "jwt:blacklist";
	const std::string kJwtWhitelistPrefix = "jwt:whitelist:user:";

	bool parseJsonString(const std::string &s, Json::Value &out, std::string &errs)
	{
		Json::CharReaderBuilder b;
		std::unique_ptr<Json::CharReader> reader(b.newCharReader());
		return reader->parse(s.data(), s.data() + s.size(), &out, &errs);
	}

	long long currentUnixSeconds()
	{
		auto now = std::chrono::system_clock::now();
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
		return seconds.count();
	}

	std::string whitelistKeyForUser(int userId)
	{
		return kJwtWhitelistPrefix + std::to_string(userId);
	}

	bool decodeTokenPayload(const std::string &token, const std::string &signingKey, int &userId, std::string &userName, std::string &error)
	{
		try
		{
			auto decoded = jwt::decode(token);
			jwt::verify()
				.allow_algorithm(jwt::algorithm::hs256{signingKey})
				.with_issuer("Signin")
				.verify(decoded);
			userId = decoded.get_payload_claim("ID").as_integer();
			userName = decoded.get_payload_claim("Name").as_string();
			return true;
		}
		catch (const std::exception &e)
		{
			error = e.what();
			return false;
		}
	}

	void addTokenToWhitelist(const drogon::nosql::RedisClientPtr &redisClient,
							 int userId,
							 const std::string &token,
							 std::function<void()> onSuccess,
							 std::function<void(const std::string &)> onError)
	{
		auto whitelistKey = whitelistKeyForUser(userId);
		auto score = currentUnixSeconds();
		redisClient->execCommandAsync(
			[redisClient, whitelistKey, onSuccess, onError](const drogon::nosql::RedisResult &r)
			{
				if (r.type() != nosql::RedisResultType::kInteger)
				{
					onError("redis_zadd_failed");
					return;
				}
				redisClient->execCommandAsync(
					[redisClient, whitelistKey, onSuccess, onError](const drogon::nosql::RedisResult &r2)
					{
						if (r2.type() != nosql::RedisResultType::kInteger)
						{
							onError("redis_zcard_failed");
							return;
						}
						auto count = r2.asInteger();
						if (count <= 2)
						{
							onSuccess();
							return;
						}
						auto extra = count - 2;
						redisClient->execCommandAsync(
							[redisClient, whitelistKey, onSuccess, onError](const drogon::nosql::RedisResult &r3)
							{
								if (r3.type() != nosql::RedisResultType::kArray)
								{
									onError("redis_zpopmin_failed");
									return;
								}
								const auto &items = r3.asArray();
								if (items.empty())
								{
									onSuccess();
									return;
								}
								std::vector<std::string> tokens;
								for (size_t i = 0; i + 1 < items.size(); i += 2)
								{
									tokens.push_back(items[i].asString());
								}
								if (tokens.empty())
								{
									onSuccess();
									return;
								}
								auto pending = std::make_shared<std::atomic<size_t>>(tokens.size());
								auto failed = std::make_shared<std::atomic<bool>>(false);
								for (const auto &removedToken : tokens)
								{
									redisClient->execCommandAsync(
										[pending, failed, onSuccess, onError](const drogon::nosql::RedisResult &r4)
										{
											if (r4.type() != nosql::RedisResultType::kInteger)
											{
												if (!failed->exchange(true))
												{
													onError("redis_sadd_failed");
													return;
												}
											}
											if (pending->fetch_sub(1) == 1 && !failed->load())
											{
												onSuccess();
											}
										},
										[pending, failed, onError](const std::exception &err)
										{
											if (!failed->exchange(true))
											{
												onError(err.what());
											}
											pending->fetch_sub(1);
										},
										"SADD %s %s", kJwtBlacklistKey.c_str(), removedToken.c_str());
								}
							},
							[onError](const std::exception &err)
							{ onError(err.what()); },
							"ZPOPMIN %s %lld", whitelistKey.c_str(), extra);
					},
					[onError](const std::exception &err)
					{ onError(err.what()); },
					"ZCARD %s", whitelistKey.c_str());
			},
			[onError](const std::exception &err)
			{ onError(err.what()); },
			"ZADD %s %lld %s", whitelistKey.c_str(), score, token.c_str());
	}
} // namespace

static bool isChannelReady(std::shared_ptr<grpc::Channel> channel)
{
	grpc_connectivity_state state =
		channel->GetState(/*try_to_connect=*/true);

	return state == GRPC_CHANNEL_READY ||
		   state == GRPC_CHANNEL_IDLE ||
		   state == GRPC_CHANNEL_CONNECTING;
}

std::shared_ptr<account::accountService::Stub>
AccountController::FindService(const std::string &key) const
{
	CloudiskConsul consul(MyAppData::instance().consulHost, MyAppData::instance().consulPort);

	return ArcGrpcLB::FindService<account::accountService>(
		cache_, consul, key, 10,
		[](const std::shared_ptr<grpc::Channel> &ch)
		{ return isChannelReady(ch); });
}

void AccountController::signup(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	std::string rid = req->getHeader("X-Request-Id");
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
	context->AddMetadata("x-request-id", rid);
	stub->async()->Signup(context.get(), request.get(), response.get(),
						  [callback, context, request, response, rid](::grpc::Status s)
						  {
							  if (response == nullptr)
							  {
								  callback(transError("error", "empty grpc response", k502BadGateway));
								  return;
							  }
							  if (!s.ok())
							  {
								  LOG_ERROR_RID(rid, "[signup] gRPC Signup failed: {} {}", (int)s.error_code(), s.error_message());
								  callback(grpcErrorResponse(s));
								  return;
							  }
							  if (s.ok() && response->code() == 0)
							  {
								  Json::Value ret;
								  ret["status"] = "ok";
								  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
								  callback(resp);
								  LOG_INFO_RID(rid, "[signup] user:{}   user registering", request->username());
								  return;
							  }
							  callback(transError("error", response->message(), k400BadRequest)); });
};

void AccountController::signin(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
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
	context->AddMetadata("x-request-id", rid);
	stub->async()->Signin(context.get(), request.get(), response.get(),
						  [callback, context, request, response, rid](::grpc::Status s)
						  {
							  if (response == nullptr)
							  {
								  callback(transError("error", "empty grpc response", k502BadGateway));
								  return;
							  }
							  if (!s.ok())
							  {
								  LOG_ERROR_RID(rid, "[signin] gRPC Signin failed: {} {}", (int)s.error_code(), s.error_message());
								  callback(grpcErrorResponse(s));
								  return;
							  }
							  if (s.ok() && response->code() == 0)
							  {
								  Json::Value ret;
								  ret["status"] = "ok";
								  ret["token"] = response->message();
								  auto token = response->message();
								  auto redisClient = app().getRedisClient();
								  if (!redisClient)
								  {
									  auto resp = transError("error", "Redis client unavailable", k500InternalServerError);
									  callback(resp);
									  return;
								  }
								  int userId = 0;
								  std::string userName;
								  std::string decodeError;
								  if (!decodeTokenPayload(token, MyAppData::instance().SigningKey, userId, userName, decodeError))
								  {
									  auto resp = transError("error", decodeError, k401Unauthorized);
									  callback(resp);
									  return;
								  }
							  addTokenToWhitelist(
								  redisClient,
								  userId,
								  token,
									  [callback, ret]()
									  {
										  auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
										  callback(resp);
									  },
									  [callback](const std::string &err)
									  {
										  auto resp = transError("error", err, k500InternalServerError);
										  callback(resp);
									  });
								  LOG_INFO_RID(rid, "[signin] user:{}   user registering", request->username());
								  return;
							  }
							  callback(transError("error", response->message(), k401Unauthorized)); });
}

void AccountController::userinfo(const HttpRequestPtr &req,
								 std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
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
	context->AddMetadata("x-request-id", rid);
	stub->async()->Userinfo(context.get(), request.get(), response.get(),
								[callback, context, request, response, rid](::grpc::Status s)
								{
								if (response == nullptr)
								{
									callback(transError("error", "empty grpc response", k502BadGateway));
									return;
								}
								if (!s.ok())
								{
									LOG_ERROR_RID(rid, "[userinfo] gRPC Userinfo failed: {} {}", (int)s.error_code(), s.error_message());
									callback(grpcErrorResponse(s));
									return;
								}
								if (s.ok() && response->code() == 0)
								{
									Json::Value payload;
									std::string errs;
									if (!parseJsonString(response->message(), payload, errs) || !payload.isObject())
									{
										auto resp = transError("error", "invalid_userinfo_payload", k502BadGateway);
										callback(resp);
										return;
									}

									Json::Value ret;
									ret["username"] = payload.isMember("username") ? payload["username"] : payload["name"];
									ret["email"] = payload.get("email", "");
									ret["name"] = payload.get("name", ret["username"]);
									ret["gender"] = payload.get("gender", "");
									ret["mobile"] = payload.isMember("mobile") ? payload["mobile"] : payload.get("Mobile", "");
									ret["createdAt"] = payload.get("createdAt", "");
									ret["updatedAt"] = payload.get("updatedAt", "");
									auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
									callback(resp);
									LOG_INFO_RID(rid, "[userinfo] user:{}   user info", request->username());
								}
								else{
									callback(transError("error", response->message(), k404NotFound));
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

void AccountController::addToBlacklist(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !jsonPtr->isMember("token"))
	{
		auto resp = transError("error", "Missing token field", k400BadRequest);
		callback(resp);
		return;
	}
	auto token = (*jsonPtr)["token"].asString();
	if (token.empty())
	{
		auto resp = transError("error", "Token is empty", k400BadRequest);
		callback(resp);
		return;
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		auto resp = transError("error", "Redis client unavailable", k500InternalServerError);
		callback(resp);
		return;
	}
	int userId = 0;
	std::string userName;
	std::string decodeError;
	if (!decodeTokenPayload(token, MyAppData::instance().SigningKey, userId, userName, decodeError))
	{
		auto resp = transError("error", decodeError, k401Unauthorized);
		callback(resp);
		return;
	}
	auto whitelistKey = whitelistKeyForUser(userId);
	redisClient->execCommandAsync(
		[callback, token, whitelistKey, redisClient](const drogon::nosql::RedisResult &r)
		{
			if (r.type() != nosql::RedisResultType::kInteger)
			{
				auto resp = transError("error", "redis_sadd_failed", k500InternalServerError);
				callback(resp);
				return;
			}
			redisClient->execCommandAsync(
				[callback](const drogon::nosql::RedisResult &)
				{
					auto resp = transError("ok", "token blacklisted", k200OK);
					callback(resp);
				},
				[callback](const std::exception &err)
				{
					auto resp = transError("error", err.what(), k500InternalServerError);
					callback(resp);
				},
				"ZREM %s %s", whitelistKey.c_str(), token.c_str());
		},
		[callback](const std::exception &err)
		{
			auto resp = transError("error", err.what(), k500InternalServerError);
			callback(resp);
		},
		"SADD %s %s", kJwtBlacklistKey.c_str(), token.c_str());
}

void AccountController::addToWhitelist(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !jsonPtr->isMember("token"))
	{
		auto resp = transError("error", "Missing token field", k400BadRequest);
		callback(resp);
		return;
	}
	auto token = (*jsonPtr)["token"].asString();
	if (token.empty())
	{
		auto resp = transError("error", "Token is empty", k400BadRequest);
		callback(resp);
		return;
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		auto resp = transError("error", "Redis client unavailable", k500InternalServerError);
		callback(resp);
		return;
	}
	int userId = 0;
	std::string userName;
	std::string decodeError;
	if (!decodeTokenPayload(token, MyAppData::instance().SigningKey, userId, userName, decodeError))
	{
		auto resp = transError("error", decodeError, k401Unauthorized);
		callback(resp);
		return;
	}
	addTokenToWhitelist(
		redisClient,
		userId,
		token,
		[callback]()
		{
			auto resp = transError("ok", "token whitelisted", k200OK);
			callback(resp);
		},
		[callback](const std::string &err)
		{
			auto resp = transError("error", err, k500InternalServerError);
			callback(resp);
		});
}
