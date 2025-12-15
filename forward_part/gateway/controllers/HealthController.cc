#include "HealthController.h"

void HealthController::health(const drogon::HttpRequestPtr &req,
							  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	auto resp = drogon::HttpResponse::newHttpResponse();
	resp->setStatusCode(drogon::k200OK);
	resp->setBody("OK");
	callback(resp);
}