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
	ADD_METHOD_TO(AccountController::sendcode, "/user/sendcode", Post);
	ADD_METHOD_TO(AccountController::verifycode, "/user/code", Post);
	ADD_METHOD_TO(AccountController::userinfo, "/user/info", Get, "jwt_decode");

	ADD_METHOD_TO(AccountController::userinfo, "/file/query", Post, "jwt_decode");
	ADD_METHOD_TO(AccountController::userinfo, "/file/upload", Post, "jwt_decode");
	ADD_METHOD_TO(AccountController::userinfo, "/file/download", Get, "jwt_decode");
	ADD_METHOD_TO(AccountController::userinfo, "/file/showfile", Get, "jwt_decode");
	ADD_METHOD_TO(AccountController::userinfo, "/file/ask", Get, "jwt_decode");
	METHOD_LIST_END
	// your declaration of processing function maybe like this:
	void signup(const HttpRequestPtr &req,
				std::function<void(const HttpResponsePtr &)> &&callback);
	void signin(const HttpRequestPtr &req,
				std::function<void(const HttpResponsePtr &)> &&callback) const;
	void userinfo(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void sendcode(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
	void verifycode(const HttpRequestPtr &req,
				  std::function<void(const HttpResponsePtr &)> &&callback) const;
};