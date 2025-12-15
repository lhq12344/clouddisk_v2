/**
 *
 *  jwt_decode.cc
 *
 */

#include "jwt_decode.h"

using namespace drogon;

void jwt_decode::doFilter(const HttpRequestPtr &req,
						  FilterCallback &&fcb,
						  FilterChainCallback &&fccb)
{
    auto auth = req->getHeader("Authorization");
    if (auth.empty() || auth.rfind("Bearer ", 0) != 0) {
        auto res = HttpResponse::newHttpResponse();
        res->setStatusCode(k401Unauthorized);
        return fcb(res);
    }

    std::string token = auth.substr(7);
    std::string signingKey = MyAppData::instance().SigningKey;

    try {
        // 默认 decode() 会使用 picojson traits
        auto decoded = jwt::decode(token);

        jwt::verify()  // 默认 verify() 也使用 picojson traits
            .allow_algorithm(jwt::algorithm::hs256{signingKey})
            .with_issuer("Signin")
            .verify(decoded);

        // 通过 as_integer() 获取 ID
        int id = decoded.get_payload_claim("ID").as_integer();
		std::string name = decoded.get_payload_claim("Name").as_string();
        req->getAttributes()->insert("ID", id);
		req->getAttributes()->insert("Name", name);
		LOG_INFO("[doFilter]jwt decode success");
        fccb();
    } catch (const std::exception& e) {
        auto res = HttpResponse::newHttpResponse();
        res->setStatusCode(k401Unauthorized);
        res->setBody(e.what());
		LOG_ERROR("[doFilter]jwt decode error");
        fcb(res);
    }
}