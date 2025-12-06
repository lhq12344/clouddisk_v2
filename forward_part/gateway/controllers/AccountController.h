#pragma once

#include <drogon/HttpController.h>
#include "../../../proto/account_srv/account.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include "../ArcCache/ArcCache.h"
#include "../ArcCache/ArcCacheNode.h"
#include "../../internal/internal.h"
#include "../../internal/consul.h"
#include <jsoncpp/json/json.h>
#include "../MyAppData.h"
#include "../../logs/Logger.h"

using namespace drogon;

class AccountController : public drogon::HttpController<AccountController>
{
private:
	const int CAPACITY;
	mutable Cache::KArcCache<std::string, ServiceInstance> cache_;
	std::shared_ptr<account::accountService::Stub> FindService(const std::string &key) const;

public:
	AccountController()
		: CAPACITY(20),
		  cache_(CAPACITY) {};

	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(AccountController::signup, "/user/signup", Post);
	ADD_METHOD_TO(AccountController::signin, "/user/signin", Post);
	METHOD_LIST_END
	// your declaration of processing function maybe like this:
	void signup(const HttpRequestPtr &req,
				std::function<void(const HttpResponsePtr &)> &&callback);
	void signin(const HttpRequestPtr &req,
				std::function<void(const HttpResponsePtr &)> &&callback) const;
};