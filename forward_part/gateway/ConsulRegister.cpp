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

	LOG_INFO("Registering service to Consul: {}:{}", consulHost, consulPort);
	LOG_INFO("Service: {} ({}), Address: {}:{}", serviceName, serviceId, address, port);
	LOG_INFO("Health check URL: http://{}:{}/health", address, port);

	auto client = drogon::HttpClient::newHttpClient(
		"http://" + consulHost + ":" + std::to_string(consulPort));

	auto req = drogon::HttpRequest::newHttpJsonRequest(body);
	req->setMethod(drogon::Put);
	req->setPath("/v1/agent/service/register");

	client->sendRequest(req, [this, client](drogon::ReqResult result, const drogon::HttpResponsePtr &resp)
						{
            if (result == drogon::ReqResult::Ok)
			{
				if (resp && resp->getStatusCode() == drogon::k200OK)
				{
					LOG_INFO("✓ Service registered to Consul successfully");
				}
				else if (resp)
				{
					LOG_ERROR("✗ Failed to register service, status: {}, body: {}",
						(int)resp->getStatusCode(), resp->getBody());
				}
				else
				{
					LOG_ERROR("✗ Failed to register service, no response");
				}
			}
            else
			{
                LOG_ERROR("✗ Failed to register service, network error: {}", (int)result);
			} });
}

void ConsulRegister::deregister()
{
	auto client = drogon::HttpClient::newHttpClient(
		"http://" + consulHost + ":" + std::to_string(consulPort));

	auto req = drogon::HttpRequest::newHttpRequest();
	req->setMethod(drogon::Put);
	req->setPath("/v1/agent/service/deregister/" + serviceId);

	client->sendRequest(req, [client](drogon::ReqResult result, const drogon::HttpResponsePtr &resp)
						{
            if (result == drogon::ReqResult::Ok)
                LOG_INFO("Service deregistered from Consul"); });
}