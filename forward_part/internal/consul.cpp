#include "consul.h"

bool CloudiskConsul::registerService(const std::string &consulHost,
									 int consulPort,
									 const std::string &serviceID,
									 const std::string &serviceName,
									 const std::string &serviceIP,
									 int servicePort)
{
	CURL *curl = curl_easy_init();
	if (!curl)
	{
		std::cerr << "[Consul] curl init failed!\n";
		return false;
	}

	std::string url = "http://" + consulHost + ":" + std::to_string(consulPort) + "/v1/agent/service/register";

	// 构造 JSON body（Consul 官方格式）
	std::string json = R"({
            "ID": ")" + serviceID +
					   R"(",
            "Name": ")" +
					   serviceName +
					   R"(",
            "Address": ")" +
					   serviceIP + R"(",
            "Port": )" +
					   std::to_string(servicePort) +
					   R"(,
            "Check": {
                "GRPC": ")" +
					   serviceIP + ":" + std::to_string(servicePort) + R"(",
                "Interval": "10s",
                "Timeout": "3s"
            }
        })";

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json.c_str());

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

	CURLcode res = curl_easy_perform(curl);
	bool ok = (res == CURLE_OK);

	if (!ok)
	{
		LOG_ERROR("[Consul] Register service failed: {}", curl_easy_strerror(res));
		return false;
	}
	else
	{
		LOG_INFO("[Consul] Service registered: name={} ip={} port={}", serviceName, serviceIP, servicePort);
	}

	curl_easy_cleanup(curl);
	return ok;
}

/*
 * 从 Consul 反注册服务
 */
bool CloudiskConsul::deregisterService(const std::string &consulHost,
									   int consulPort,
									   const std::string &serviceID)
{
	CURL *curl = curl_easy_init();
	if (!curl)
		return false;

	std::string url = "http://" + consulHost + ":" + std::to_string(consulPort) + "/v1/agent/service/deregister/" + serviceID;

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");

	CURLcode res = curl_easy_perform(curl);
	bool ok = (res == CURLE_OK);

	if (!ok)
	{
		LOG_ERROR("[Consul] Deregister failed: {}", curl_easy_strerror(res));
		return false;
	}
	else
	{
		LOG_INFO("[Consul] Service deregistered:{} ", serviceID);
	}

	curl_easy_cleanup(curl);
	return ok;
}

/*
 * libcurl 写入 buffer 回调
 */
size_t CloudiskConsul::WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s)
{
	size_t total = size * nmemb;
	s->append((char *)contents, total);
	return total;
}

/*
 * 从 Consul 获取服务实例列表
 */
std::vector<ServiceInstance> CloudiskConsul::getServiceInstances(const std::string &serviceName)
{
	std::vector<ServiceInstance> instances;

	CURL *curl = curl_easy_init();
	if (!curl)
		return instances;

	std::string url = "http://" + consulHost + ":" + std::to_string(consulPort) + "/v1/health/service/" + serviceName + "?passing";

	std::string response;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK)
	{
		std::cerr << "[ServiceDiscovery] Request failed: "
				  << curl_easy_strerror(res) << "\n";
		curl_easy_cleanup(curl);
		return instances;
	}

	curl_easy_cleanup(curl);

	try
	{
		auto arr = json::parse(response);

		for (auto &item : arr)
		{
			auto srv = item["Service"];
			ServiceInstance inst{
				srv["Address"].get<std::string>(),
				srv["Port"].get<int>()};
			instances.push_back(inst);
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << "[ServiceDiscovery] JSON parse error: " << e.what() << "\n";
	}

	return instances;
}

/*
 * 随机负载均衡：返回一个实例
 */
ServiceInstance CloudiskConsul::getRandomInstance(const std::string &svc)
{
	auto list = getServiceInstances(svc);
	if (list.empty())
		return {"", 0};

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0, list.size() - 1);

	return list[dis(gen)];
}

/*
 * 轮询负载均衡：每次取下一个实例
 */
ServiceInstance CloudiskConsul::getRoundRobinInstance(const std::string &svc)
{
	auto list = getServiceInstances(svc);
	if (list.empty())
		return {"", 0};

	size_t idx = rrIndex++ % list.size();
	return list[idx];
}