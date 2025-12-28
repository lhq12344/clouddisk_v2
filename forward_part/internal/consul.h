#ifndef CONSUL_H
#define CONSUL_H
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <curl/curl.h>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include "../logs/Logger.h"

using json = nlohmann::json;

struct BasicInstance
{
	std::string address;
	int port{0};
};

class CloudiskConsul
{
private:
	std::string consulHost;
	int consulPort;
	std::atomic<size_t> rrIndex{0};

private:
	static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s);
	std::vector<BasicInstance> getServiceInstances(const std::string &serviceName);

public:
	CloudiskConsul(const std::string &host, int port)
		: consulHost(host), consulPort(port) {}

	static bool registerService(const std::string &consulHost,
								int consulPort,
								const std::string &serviceID,
								const std::string &serviceName,
								const std::string &serviceIP,
								int servicePort);

	static bool deregisterService(const std::string &consulHost,
								  int consulPort,
								  const std::string &serviceID);

	// 新增：返回全部 passing 实例
	std::vector<BasicInstance> getAllPassingInstances(const std::string &svc)
	{
		return getServiceInstances(svc);
	}

	BasicInstance getRandomInstance(const std::string &svc);
	BasicInstance getRoundRobinInstance(const std::string &svc);
};

#endif // !CONSUL_H
