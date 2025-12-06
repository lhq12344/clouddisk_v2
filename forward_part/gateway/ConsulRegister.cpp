#include "ConsulRegister.h"

void ConsulRegister::registerService()
{
	Json::Value body;
	body["Name"] = serviceName;
	body["ID"] = serviceId;
	body["Address"] = address;
	body["Port"] = port;

	// 健康检查
	body["Check"]["DeregisterCriticalServiceAfter"] = "30s";
	body["Check"]["HTTP"] = "http://" + address + ":" + std::to_string(port) + "/health";
	body["Check"]["Interval"] = "5s";

	auto client = drogon::HttpClient::newHttpClient(
		"http://" + consulHost + ":" + std::to_string(consulPort));

	auto req = drogon::HttpRequest::newHttpJsonRequest(body);
	req->setMethod(drogon::Put);
	req->setPath("/v1/agent/service/register");

	client->sendRequest(req, [](drogon::ReqResult result, const drogon::HttpResponsePtr &resp)
						{
            if (result == drogon::ReqResult::Ok)
                LOG_INFO("Service registered to Consul");
            else
                LOG_ERROR("Failed to register service"); });
}

void ConsulRegister::deregister()
{
	auto client = drogon::HttpClient::newHttpClient(
		"http://" + consulHost + ":" + std::to_string(consulPort));

	auto req = drogon::HttpRequest::newHttpRequest();
	req->setMethod(drogon::Put);
	req->setPath("/v1/agent/service/deregister/" + serviceId);

	client->sendRequest(req, [](drogon::ReqResult result, const drogon::HttpResponsePtr &resp)
						{
            if (result == drogon::ReqResult::Ok)
                LOG_INFO("Service deregistered from Consul"); });
}