#include <drogon/drogon.h>
#include "../internal/internal.h"
#include "ConsulRegister.h"
#include "MyAppData.h"
int main()
{
	// 获取ip和port
	InitAppConfig();
	auto &cfg = AppConfig::getInstance();
	std::string host = cfg.consul.gateway_srv.host;
	int port = GetFreePort();
	std::string consulHost = cfg.consul.host;
	int consulPort = std::atoi(cfg.consul.port.c_str());
	std::string serviceName = "gateway_srv";
	std::string serviceId = serviceName + std::to_string(port);
	ConsulRegister consulRegister(
		consulHost,
		consulPort,
		serviceName,
		serviceId,
		host,
		port);
	// 启动 Drogon HTTP 服务
	drogon::app().addListener(host, port);

	// 启动前注册到 Consul
	drogon::app().registerBeginningAdvice([&]()
										  { 
											MyAppData::instance().consulHost = consulHost;
											MyAppData::instance().consulPort = consulPort;
											consulRegister.registerService(); });
	LOG_INFO("[drogon]Server started:{}:{} ", host, port);
	drogon::app().run();
	return 0;
}
