#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
using namespace drogon;

class HealthController : public drogon::HttpController<HealthController>
{
public:
	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(HealthController::health, "/health", Get);
	METHOD_LIST_END
	void health(const drogon::HttpRequestPtr &req,
				std::function<void(const drogon::HttpResponsePtr &)> &&callback);
};
