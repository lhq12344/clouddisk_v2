#pragma once
#include <drogon/drogon.h>
#include <jsoncpp/json/json.h>
#include "../logs/Logger.h"
class ConsulRegister
{
private:
	const std::string consulHost;
	int consulPort;
	const std::string serviceName;
	const std::string serviceId;
	const std::string address;
	int port;

private:
	void deregister();

public:
	ConsulRegister(const std::string &consulHost,
				   int consulPort,
				   const std::string &serviceName,
				   const std::string &serviceId,
				   const std::string &address,
				   int port)
		: consulHost(consulHost),
		  consulPort(consulPort),
		  serviceName(serviceName),
		  serviceId(serviceId),
		  address(address),
		  port(port) {
		  };
	~ConsulRegister()
	{
		deregister();
	};
	void registerService();
};
