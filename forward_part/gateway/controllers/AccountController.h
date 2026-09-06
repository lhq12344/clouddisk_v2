#pragma once

#include <drogon/HttpController.h>
#include <jsoncpp/json/json.h>
#include <functional>
#include "../MyAppData.h"
#include "../../logs/Logger.h"

using namespace drogon;

class AccountController : public drogon::HttpController<AccountController>
{
private:
	const int CAPACITY;

public:
	AccountController()
		: CAPACITY(20) {};

	METHOD_LIST_BEGIN
	// use METHOD_ADD to add your custom processing function here;
	ADD_METHOD_TO(AccountController::signup, "/user/signup", Post);
	ADD_METHOD_TO(AccountController::signin, "/user/signin", Post);
	ADD_METHOD_TO(AccountController::sendcode, "/user/sendcode", Post);
	ADD_METHOD_TO(AccountController::verifycode, "/user/code", Post);
	ADD_METHOD_TO(AccountController::userinfo, "/user/info", Get, "jwt_decode");
	ADD_METHOD_TO(AccountController::addToBlacklist, "/user/token/blacklist", Post, "jwt_decode");
	ADD_METHOD_TO(AccountController::addToWhitelist, "/user/token/whitelist", Post, "jwt_decode");

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
	void addToBlacklist(const HttpRequestPtr &req,
						std::function<void(const HttpResponsePtr &)> &&callback) const;
	void addToWhitelist(const HttpRequestPtr &req,
						std::function<void(const HttpResponsePtr &)> &&callback) const;
};
