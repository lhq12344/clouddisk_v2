#include "HealthController.h"

#include "../MyAppData.h"
#include "../infrastructure/runtime/CoreRuntime.h"
#include "../application/common/FeatureFlags.h"

#include <drogon/drogon.h>
#include <drogon/nosql/RedisClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>

#include <exception>
#include <memory>
#include <mutex>
#include <string>

namespace
{
struct ReadyProbeState
{
	Json::Value body;
	bool ready{true};
	int pending{0};
	std::mutex mutex;
	std::function<void(const drogon::HttpResponsePtr &)> callback;
};

Json::Value dependencyToJson(const core::infrastructure::runtime::DependencyStatus &dependency)
{
	Json::Value item;
	item["name"] = dependency.name;
	item["configured"] = dependency.configured;
	item["ready"] = dependency.ready;
	item["detail"] = dependency.detail;
	return item;
}

Json::Value flagsToJson(const core::application::CoreFeatureFlags &flags)
{
	Json::Value value;
	value["core.account.reads"] = core::application::toString(flags.accountReads);
	value["core.account.login"] = core::application::toString(flags.accountLogin);
	value["core.account.registration"] = core::application::toString(flags.accountRegistration);
	value["core.file.reads"] = core::application::toString(flags.fileReads);
	value["core.file.access"] = core::application::toString(flags.fileAccess);
	value["core.upload.control"] = core::application::toString(flags.uploadControl);
	value["core.upload.complete"] = core::application::toString(flags.uploadComplete);
	value["core.file.delete"] = core::application::toString(flags.fileDelete);
	value["core.small_upload.direct"] = core::application::toString(flags.smallUploadDirect);
	value["core.outbox.relay"] = core::application::toString(flags.outboxRelay);
	return value;
}

void setDependencyProbe(Json::Value &body,
						const std::string &name,
						bool ready,
						const std::string &detail)
{
	auto &dependencies = body["dependencies"];
	for (auto &dependency : dependencies)
	{
		if (dependency.get("name", "").asString() == name)
		{
			dependency["ready"] = dependency.get("configured", false).asBool() && ready;
			dependency["detail"] = detail;
			return;
		}
	}

	Json::Value dependency;
	dependency["name"] = name;
	dependency["configured"] = false;
	dependency["ready"] = false;
	dependency["detail"] = detail;
	dependencies.append(dependency);
}

void finishReadyProbe(const std::shared_ptr<ReadyProbeState> &state)
{
	state->body["status"] = state->ready ? "ready" : "not_ready";
	auto resp = drogon::HttpResponse::newHttpJsonResponse(state->body);
	resp->setStatusCode(state->ready ? drogon::k200OK : drogon::k503ServiceUnavailable);
	state->callback(resp);
}

void completeReadyProbe(const std::shared_ptr<ReadyProbeState> &state,
						const std::string &name,
						bool ready,
						const std::string &detail)
{
	bool shouldFinish = false;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		setDependencyProbe(state->body, name, ready, detail);
		if (!ready)
		{
			state->ready = false;
		}
		state->pending--;
		shouldFinish = state->pending == 0;
	}
	if (shouldFinish)
	{
		finishReadyProbe(state);
	}
}
} // namespace

void HealthController::health(const drogon::HttpRequestPtr &req,
							  std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	(void)req;
	auto resp = drogon::HttpResponse::newHttpResponse();
	resp->setStatusCode(drogon::k200OK);
	resp->setBody("OK");
	callback(resp);
}

void HealthController::ready(const drogon::HttpRequestPtr &req,
							 std::function<void(const drogon::HttpResponsePtr &)> &&callback)
{
	(void)req;
	auto snapshot = core::infrastructure::runtime::CoreRuntime::instance().snapshot();
	Json::Value body;
	body["status"] = snapshot.ready ? "ready" : "not_ready";
	body["initialized"] = snapshot.initialized;
	body["config_source"] = snapshot.configSource;
	body["config_version"] = snapshot.configVersion;
	body["feature_flags"] = flagsToJson(snapshot.flags);
	body["dependencies"] = Json::Value(Json::arrayValue);
	for (const auto &dependency : snapshot.dependencies)
	{
		body["dependencies"].append(dependencyToJson(dependency));
	}

	auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
	if (!snapshot.ready)
	{
		resp->setStatusCode(drogon::k503ServiceUnavailable);
		callback(resp);
		return;
	}

	auto state = std::make_shared<ReadyProbeState>();
	state->body = body;
	state->ready = true;
	state->pending = 2;
	state->callback = std::move(callback);

	auto mysqlClient = MyAppData::instance().mysqlClient;
	if (!mysqlClient)
	{
		completeReadyProbe(state, "mysql", false, "mysql client is not initialized");
	}
	else
	{
		mysqlClient->execSqlAsync(
			[state](const drogon::orm::Result &) {
				completeReadyProbe(state, "mysql", true, "select 1 ok");
			},
			[state](const drogon::orm::DrogonDbException &e) {
				completeReadyProbe(state, "mysql", false, e.base().what());
			},
			"select 1");
	}

	auto redisClient = drogon::app().getRedisClient();
	if (!redisClient)
	{
		completeReadyProbe(state, "redis", false, "redis client is not initialized");
	}
	else
	{
		redisClient->execCommandAsync(
			[state](const drogon::nosql::RedisResult &) {
				completeReadyProbe(state, "redis", true, "PING ok");
			},
			[state](const std::exception &e) {
				completeReadyProbe(state, "redis", false, e.what());
			},
			"PING");
	}
}
