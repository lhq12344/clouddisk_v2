#include <drogon/drogon.h>
#include "../internal/internal.h"
#include "ConsulRegister.h"
#include "MyAppData.h"
int main()
{
	// 获取ip和port
	InitAppConfig();
	auto &cfg = AppConfig::getInstance();
	std::string registerHost = cfg.consul.gateway_srv.host; // 注册到 Consul 的地址
	std::string listenHost = "0.0.0.0"; // 监听地址
	int port = GetFreePort();
	std::string SigningKey = cfg.jwt.secret;
	std::string consulHost = cfg.consul.host;
	int consulPort = std::atoi(cfg.consul.port.c_str());
	std::string kafkaHost = cfg.kafka.host;
	int kafkaPort = std::atoi(cfg.kafka.port.c_str());
	std::string redisHost = cfg.redis.host;
	int redisPort = std::atoi(cfg.redis.port.c_str());
	drogon::app().createRedisClient(redisHost, redisPort);
	std::string serviceName = "gateway_srv";
	std::string serviceId = serviceName + std::to_string(port);
	ConsulRegister consulRegister(
		consulHost,
		consulPort,
		serviceName,
		serviceId,
		registerHost, // 使用注册地址
		port);
	// 启动 Drogon HTTP 服务
	drogon::app().addListener(listenHost, port); // 监听在 0.0.0.0

	// 在服务器启动后注册到 Consul
	drogon::app().registerBeginningAdvice([&]()
										  {
											MyAppData::instance().consulHost = consulHost;
											MyAppData::instance().consulPort = consulPort;
											MyAppData::instance().kafkaHost = kafkaHost;
											MyAppData::instance().kafkaPort = kafkaPort;
											MyAppData::instance().SigningKey = SigningKey;

											// 延迟注册，确保事件循环已启动
											drogon::app().getLoop()->queueInLoop([&consulRegister]() {
												consulRegister.registerService();
											}); });
	LOG_INFO("[drogon]Server started:{}:{} ", listenHost, port);
	drogon::app().run();
	return 0;
}
