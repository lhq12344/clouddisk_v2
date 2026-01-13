#include "jwt_decode.h"

using namespace drogon;

void jwt_decode::doFilter(const HttpRequestPtr &req,
						  FilterCallback &&fcb,
						  FilterChainCallback &&fccb)
{
	auto auth = req->getHeader("Authorization");
	if (auth.empty() || auth.rfind("Bearer ", 0) != 0)
	{
		auto res = HttpResponse::newHttpResponse();
		res->setStatusCode(k401Unauthorized);
		return fcb(res);
	}

	std::string token = auth.substr(7);
	std::string signingKey = MyAppData::instance().SigningKey;

	try
	{
		// 默认 decode() 会使用 picojson traits
		auto decoded = jwt::decode(token);

		jwt::verify() // 默认 verify() 也使用 picojson traits
			.allow_algorithm(jwt::algorithm::hs256{signingKey})
			.with_issuer("Signin")
			.verify(decoded);

		// 通过 as_integer() 获取 ID
		int id = decoded.get_payload_claim("ID").as_integer();
		std::string name = decoded.get_payload_claim("Name").as_string();
		auto redisClient = app().getRedisClient();
		if (!redisClient)
		{
			auto res = HttpResponse::newHttpResponse();
			res->setStatusCode(k500InternalServerError);
			res->setBody("Redis client unavailable");
			return fcb(res);
		}
		std::string blacklistKey = "jwt:blacklist";
		std::string whitelistKey = "jwt:whitelist:user:" + std::to_string(id);
		redisClient->execCommandAsync(
			[=](const drogon::nosql::RedisResult &r)
			{
				if (r.type() == nosql::RedisResultType::kInteger && r.asInteger() == 1)
				{
					auto res = HttpResponse::newHttpResponse();
					res->setStatusCode(k401Unauthorized);
					res->setBody("Token blacklisted");
					return fcb(res);
				}
				redisClient->execCommandAsync(
					[=](const drogon::nosql::RedisResult &r2)
					{
						if (r2.type() == nosql::RedisResultType::kNil)
						{
							auto res = HttpResponse::newHttpResponse();
							res->setStatusCode(k401Unauthorized);
							res->setBody("Token not whitelisted");
							return fcb(res);
						}
						req->getAttributes()->insert("ID", id);
						req->getAttributes()->insert("Name", name);
						LOG_INFO("[doFilter]jwt decode success");
						fccb();
					},
					[fcb](const std::exception &err)
					{
						auto res = HttpResponse::newHttpResponse();
						res->setStatusCode(k500InternalServerError);
						res->setBody(err.what());
						fcb(res);
					},
					"ZSCORE %s %s", whitelistKey.c_str(), token.c_str());
			},
			[fcb](const std::exception &err)
			{
				auto res = HttpResponse::newHttpResponse();
				res->setStatusCode(k500InternalServerError);
				res->setBody(err.what());
				fcb(res);
			},
			"SISMEMBER %s %s", blacklistKey.c_str(), token.c_str());
	}
	catch (const std::exception &e)
	{
		auto res = HttpResponse::newHttpResponse();
		res->setStatusCode(k401Unauthorized);
		res->setBody(e.what());
		LOG_ERROR("[doFilter]jwt decode error");
		fcb(res);
	}
}
