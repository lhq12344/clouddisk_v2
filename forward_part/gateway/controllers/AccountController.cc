#include "AccountController.h"
#include "../application/account/EmailVerificationCommandService.h"
#include "../application/account/RegistrationService.h"
#include "../application/account/SigninService.h"
#include "../application/account/UserInfoService.h"
#include "../domain/common/RequestContext.h"
#include "../infrastructure/jwt/LegacyJwtIssuer.h"
#include "../infrastructure/jwt/LegacyPasswordVerifier.h"
#include "../infrastructure/mysql/MysqlAccountRepository.h"
#include "../infrastructure/outbox/MysqlOutboxRepository.h"
#include "../infrastructure/redis/RedisPendingRegistrationRepository.h"
#include "../infrastructure/runtime/CoreRuntime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
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

	drogon::HttpStatusCode httpStatusForCoreError(core::domain::ErrorCode code)
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
			return k409Conflict;
		case core::domain::ErrorCode::FailedPrecondition:
			return k412PreconditionFailed;
		case core::domain::ErrorCode::DependencyUnavailable:
			return k503ServiceUnavailable;
		case core::domain::ErrorCode::Internal:
		case core::domain::ErrorCode::None:
		default:
			return k500InternalServerError;
		}
	}

	Json::Value userInfoToJson(const core::application::account::UserInfo &info)
	{
		Json::Value ret;
		ret["username"] = info.username;
		ret["email"] = info.email;
		ret["name"] = info.username;
		ret["gender"] = info.gender;
		ret["mobile"] = info.mobile;
		ret["createdAt"] = "";
		ret["updatedAt"] = "";
		return ret;
	}

	bool requestIdentity(const HttpRequestPtr &req, core::domain::RequestContext &ctx, Json::Value &errorBody)
	{
		ctx.requestId = req->getHeader("X-Request-Id");
		try
		{
			ctx.username = req->getAttributes()->get<std::string>("Name");
			ctx.userId = static_cast<std::uint64_t>(req->getAttributes()->get<int>("ID"));
			return true;
		}
		catch (const std::exception &e)
		{
			errorBody["error"] = "missing_identity";
			errorBody["details"] = e.what();
			return false;
		}
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

void AccountController::signup(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	std::string rid = req->getHeader("X-Request-Id");
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

	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.accountRegistration == core::application::ImplementationRoute::Legacy)
	{
		callback(transError("error", "legacy account adapter removed; use CORE_ACCOUNT_REGISTRATION=core", k503ServiceUnavailable));
		return;
	}

	const auto username = (*jsonPtr)["username"].asString();
	const auto password = (*jsonPtr)["password"].asString();
	const auto email = (*jsonPtr)["email"].asString();
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		(*callbackPtr)(transError("error", "Redis client unavailable", k500InternalServerError));
		return;
	}
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlAccountRepository>(MyAppData::instance().mysqlClient);
	auto pendingRepository = std::make_shared<core::infrastructure::redis::RedisPendingRegistrationRepository>(redisClient);
	auto passwordCodec = std::make_shared<core::infrastructure::jwt::LegacyPasswordVerifier>();
	auto service = std::make_shared<core::application::account::RegistrationService>(*repository, *repository, *pendingRepository, *passwordCodec, runtimeSnapshot.flags);
	core::application::account::SignupCommand command{username, password, email};
	service->signup(command, [callbackPtr, repository, pendingRepository, passwordCodec, service, rid, username](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(transError("error", result.error.message, httpStatusForCoreError(result.error.code)));
			return;
		}
		Json::Value ret;
		ret["status"] = "ok";
		(*callbackPtr)(drogon::HttpResponse::newHttpJsonResponse(ret));
		LOG_INFO_RID(rid, "[signup] core user:{} pending registration stored", username);
	});
}

void AccountController::signin(const drogon::HttpRequestPtr &req,
							   std::function<void(const drogon::HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
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

	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.accountLogin == core::application::ImplementationRoute::Legacy)
	{
		callback(transError("error", "legacy account adapter removed; use CORE_ACCOUNT_LOGIN=core", k503ServiceUnavailable));
		return;
	}

	const auto username = (*jsonPtr)["username"].asString();
	const auto password = (*jsonPtr)["password"].asString();
	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlAccountRepository>(MyAppData::instance().mysqlClient);
	auto passwordVerifier = std::make_shared<core::infrastructure::jwt::LegacyPasswordVerifier>();
	auto jwtIssuer = std::make_shared<core::infrastructure::jwt::LegacyJwtIssuer>(MyAppData::instance().SigningKey);
	auto service = std::make_shared<core::application::account::SigninService>(*repository, *passwordVerifier, *jwtIssuer, runtimeSnapshot.flags);
	core::application::account::SigninCommand command{username, password};
	service->signin(command, [callbackPtr, repository, passwordVerifier, jwtIssuer, service, rid](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(transError("error", result.error.message, httpStatusForCoreError(result.error.code)));
			return;
		}

		Json::Value ret;
		ret["status"] = "ok";
		ret["token"] = result.value;
		auto redisClient = app().getRedisClient();
		if (!redisClient)
		{
			(*callbackPtr)(transError("error", "Redis client unavailable", k500InternalServerError));
			return;
		}
		int userId = 0;
		std::string userName;
		std::string decodeError;
		if (!decodeTokenPayload(result.value, MyAppData::instance().SigningKey, userId, userName, decodeError))
		{
			(*callbackPtr)(transError("error", decodeError, k401Unauthorized));
			return;
		}
		addTokenToWhitelist(
			redisClient,
			userId,
			result.value,
			[callbackPtr, ret]() {
				(*callbackPtr)(drogon::HttpResponse::newHttpJsonResponse(ret));
			},
			[callbackPtr](const std::string &err) {
				(*callbackPtr)(transError("error", err, k500InternalServerError));
			});
		LOG_INFO_RID(rid, "[signin] core user signed in");
	});
}

void AccountController::userinfo(const HttpRequestPtr &req,
								 std::function<void(const HttpResponsePtr &)> &&callback) const
{
	std::string rid = req->getHeader("X-Request-Id");
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.accountReads == core::application::ImplementationRoute::Legacy)
	{
		callback(transError("error", "legacy account adapter removed; use CORE_ACCOUNT_READS=core", k503ServiceUnavailable));
		return;
	}

	core::domain::RequestContext requestContext;
	Json::Value identityError;
	if (!requestIdentity(req, requestContext, identityError))
	{
		auto resp = drogon::HttpResponse::newHttpJsonResponse(identityError);
		resp->setStatusCode(k401Unauthorized);
		callback(resp);
		return;
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlAccountRepository>(MyAppData::instance().mysqlClient);
	auto service = std::make_shared<core::application::account::UserInfoService>(*repository, runtimeSnapshot.flags);
	service->getUserInfo(requestContext, [callbackPtr, repository, service, rid](auto result) {
		if (result.ok())
		{
			(*callbackPtr)(drogon::HttpResponse::newHttpJsonResponse(userInfoToJson(result.value)));
			LOG_INFO_RID(rid, "[userinfo] core user:{} user info", result.value.username);
			return;
		}

		Json::Value body;
		body["error"] = "core_account_read_failed";
		body["details"] = result.error.message;
		auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
		resp->setStatusCode(httpStatusForCoreError(result.error.code));
		(*callbackPtr)(resp);
	});
}

void AccountController::sendcode(const HttpRequestPtr &req,
								 std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto json = req->getJsonObject();
	if (!json)
	{
		return callback(transError("error", "Invalid JSON", k400BadRequest));
	}
	std::string email = (*json)["email"].asString();
	if (email.empty())
	{
		return callback(transError("error", "Email is empty", k400BadRequest));
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.accountRegistration == core::application::ImplementationRoute::Legacy)
	{
		return callback(transError("error", "legacy account adapter removed; use CORE_ACCOUNT_REGISTRATION=core", k503ServiceUnavailable));
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto outbox = std::make_shared<core::infrastructure::outbox::MysqlOutboxRepository>(MyAppData::instance().mysqlClient);
	auto service = std::make_shared<core::application::account::EmailVerificationCommandService>(*outbox, runtimeSnapshot.flags);
	core::application::account::SendVerificationCodeCommand command{email, req->getHeader("X-Request-Id")};
	service->sendVerificationCode(command, [callbackPtr, outbox, service, email](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(transError("error", result.error.message, httpStatusForCoreError(result.error.code)));
			return;
		}
		(*callbackPtr)(transError("ok", "code send successfully", k200OK));
		LOG_INFO("[sendcode] core email verification command queued for {}", email);
	});
}

void AccountController::verifycode(const HttpRequestPtr &req,
								   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto json = req->getJsonObject();
	if (!json)
	{
		callback(transError("error", "Invalid JSON", k400BadRequest));
		return;
	}
	if (!json->isMember("email") || !json->isMember("code"))
	{
		return callback(transError("error", "Missing email or code field", k400BadRequest));
	}
	std::string email = (*json)["email"].asString();
	std::string code = (*json)["code"].asString();
	if (email.empty() || code.empty())
	{
		return callback(transError("error", "Email or code is empty", k400BadRequest));
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		callback(transError("error", "Redis client unavailable", k500InternalServerError));
		return;
	}
	auto runtimeSnapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	if (runtimeSnapshot.flags.accountRegistration == core::application::ImplementationRoute::Legacy)
	{
		return callback(transError("error", "legacy account adapter removed; use CORE_ACCOUNT_REGISTRATION=core", k503ServiceUnavailable));
	}

	auto callbackPtr = std::make_shared<std::function<void(const HttpResponsePtr &)>>(std::move(callback));
	auto repository = std::make_shared<core::infrastructure::mysql::MysqlAccountRepository>(MyAppData::instance().mysqlClient);
	auto pendingRepository = std::make_shared<core::infrastructure::redis::RedisPendingRegistrationRepository>(redisClient);
	auto passwordCodec = std::make_shared<core::infrastructure::jwt::LegacyPasswordVerifier>();
	auto service = std::make_shared<core::application::account::RegistrationService>(*repository, *repository, *pendingRepository, *passwordCodec, runtimeSnapshot.flags);
	core::application::account::VerifyCodeCommand command{email, code};
	service->verifyCode(command, [callbackPtr, repository, pendingRepository, passwordCodec, service, rid = req->getHeader("X-Request-Id"), email](auto result) {
		if (!result.ok())
		{
			(*callbackPtr)(transError("error", result.error.message, httpStatusForCoreError(result.error.code)));
			return;
		}
		(*callbackPtr)(transError("ok", "REGISTRATION COMPLETE", k200OK));
		LOG_INFO_RID(rid, "[verifycode] core registration completed for email:{}", email);
	});
}

void AccountController::addToBlacklist(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !jsonPtr->isMember("token"))
	{
		callback(transError("error", "Missing token field", k400BadRequest));
		return;
	}
	auto token = (*jsonPtr)["token"].asString();
	if (token.empty())
	{
		callback(transError("error", "Token is empty", k400BadRequest));
		return;
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		callback(transError("error", "Redis client unavailable", k500InternalServerError));
		return;
	}
	int userId = 0;
	std::string userName;
	std::string decodeError;
	if (!decodeTokenPayload(token, MyAppData::instance().SigningKey, userId, userName, decodeError))
	{
		callback(transError("error", decodeError, k401Unauthorized));
		return;
	}
	auto whitelistKey = whitelistKeyForUser(userId);
	redisClient->execCommandAsync(
		[callback, token, whitelistKey, redisClient](const drogon::nosql::RedisResult &r)
		{
			if (r.type() != nosql::RedisResultType::kInteger)
			{
				callback(transError("error", "redis_sadd_failed", k500InternalServerError));
				return;
			}
			redisClient->execCommandAsync(
				[callback](const drogon::nosql::RedisResult &)
				{
					callback(transError("ok", "token blacklisted", k200OK));
				},
				[callback](const std::exception &err)
				{
					callback(transError("error", err.what(), k500InternalServerError));
				},
				"ZREM %s %s", whitelistKey.c_str(), token.c_str());
		},
		[callback](const std::exception &err)
		{
			callback(transError("error", err.what(), k500InternalServerError));
		},
		"SADD %s %s", kJwtBlacklistKey.c_str(), token.c_str());
}

void AccountController::addToWhitelist(const HttpRequestPtr &req,
									   std::function<void(const HttpResponsePtr &)> &&callback) const
{
	auto jsonPtr = req->getJsonObject();
	if (!jsonPtr || !jsonPtr->isMember("token"))
	{
		callback(transError("error", "Missing token field", k400BadRequest));
		return;
	}
	auto token = (*jsonPtr)["token"].asString();
	if (token.empty())
	{
		callback(transError("error", "Token is empty", k400BadRequest));
		return;
	}
	auto redisClient = app().getRedisClient();
	if (!redisClient)
	{
		callback(transError("error", "Redis client unavailable", k500InternalServerError));
		return;
	}
	int userId = 0;
	std::string userName;
	std::string decodeError;
	if (!decodeTokenPayload(token, MyAppData::instance().SigningKey, userId, userName, decodeError))
	{
		callback(transError("error", decodeError, k401Unauthorized));
		return;
	}
	addTokenToWhitelist(
		redisClient,
		userId,
		token,
		[callback]()
		{
			callback(transError("ok", "token whitelisted", k200OK));
		},
		[callback](const std::string &err)
		{
			callback(transError("error", err, k500InternalServerError));
		});
}
