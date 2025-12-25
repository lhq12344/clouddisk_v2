#pragma once

#include <drogon/HttpController.h>
#include "../../../proto/AI_srv/ai.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include "../ArcCache/ArcCache.h"
#include "../ArcCache/ArcCacheNode.h"
#include "../../internal/internal.h"
#include "../../internal/consul.h"
#include <jsoncpp/json/json.h>
#include "../MyAppData.h"
#include "../../logs/Logger.h"

using namespace drogon;

class AIController : public drogon::HttpController<AIController>
{
private:
	const int CAPACITY;
	mutable Cache::KArcCache<std::string, ServiceInstance> cache_;
	std::shared_ptr<AI::AIService::Stub> FindService(const std::string &key) const;

public:
	AIController()
		: CAPACITY(20),
		  cache_(CAPACITY) {};

	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(AIController::aiRequest, "/AI", Post, "jwt_decode");

	METHOD_LIST_END
	// your declaration of processing function maybe like this:
	void aiRequest(const HttpRequestPtr &req,
				   std::function<void(const HttpResponsePtr &)> &&callback);
};