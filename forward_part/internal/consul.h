#ifndef CONSUL_H
#define CONSUL_H
#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <curl/curl.h>
#include <random>
#include <mutex>
#include <atomic>
#include <nlohmann/json.hpp>
#include <grpcpp/grpcpp.h>
#include "../logs/Logger.h"
using json = nlohmann::json;

struct ServiceInstance
{
	std::string address;
	int port;
	std::shared_ptr<grpc::Channel> channel;
};

class CloudiskConsul
{
private:
	std::string consulHost;
	int consulPort;
	std::atomic<size_t> rrIndex{0}; // 轮询计数器
	std::mutex mtx;

private:
	/* libcurl 写入 buffer 回调*/
	static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *s);
	/*从 Consul 获取服务实例列表 */
	std::vector<ServiceInstance> getServiceInstances(const std::string &serviceName);

public:
	CloudiskConsul(const std::string &host, int port)
		: consulHost(host), consulPort(port) {}
	/*注册服务到 Consul*/
	static bool registerService(const std::string &consulHost,
								int consulPort,
								const std::string &serviceID,
								const std::string &serviceName,
								const std::string &serviceIP,
								int servicePort);
	/*从 Consul 反注册服务 */
	static bool deregisterService(const std::string &consulHost,
								  int consulPort,
								  const std::string &serviceID);
	/*随机负载均衡：返回一个实例*/
	ServiceInstance getRandomInstance(const std::string &svc);

	/*轮询负载均衡：每次取下一个实例 */
	ServiceInstance getRoundRobinInstance(const std::string &svc);
};

#endif // !CONSUL_H
