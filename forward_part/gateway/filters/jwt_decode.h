/**
 *
 *  jwt_decode.h
 *
 */

#pragma once

#include <drogon/HttpFilter.h>
#include <drogon/drogon.h>
#include <nlohmann/json.hpp>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include "../MyAppData.h"
#include "../../logs/Logger.h"
using namespace drogon;

class jwt_decode : public HttpFilter<jwt_decode>
{
public:
	jwt_decode() {}
	void doFilter(const HttpRequestPtr &req,
				  FilterCallback &&fcb,
				  FilterChainCallback &&fccb) override;
};
