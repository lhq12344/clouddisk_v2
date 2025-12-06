#include "accountServer.h"
#include <nacos/Nacos.h>
#include "internal/internal.h"
#include "internal/consul.h"
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

int main()
{
	// 获取ip：port
	InitAppConfig();
	auto &cfg = AppConfig::getInstance();
	std::string host = cfg.consul.account_srv.host;
	int port = GetFreePort();
	std::string address = host + ":" + std::to_string(port);

	// ① 启用 gRPC Health Check（Consul 必须）
	grpc::EnableDefaultHealthCheckService(true);
	grpc::reflection::InitProtoReflectionServerBuilderPlugin();

	// ② 构建 gRPC Server
	grpc::ServerBuilder builder;
	builder.AddListeningPort(address, grpc::InsecureServerCredentials());

	AccountServiceImpl service;
	builder.RegisterService(&service);

	std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
	LOG_INFO("[account_srv]gRPC Server SERVING at: {}", address);
	// ③ Consul 注册（GRPC 健康检查）
	std::string serviceName = "account_srv";
	std::string serviceID = serviceName + "_" + std::to_string(port);
	std::string serviceIP = host;

	CloudiskConsul::registerService(
		"192.168.149.128", 30500,
		serviceID,
		serviceName,
		serviceIP,
		port);

	// ④ 等待（阻塞）
	server->Wait();

	// ⑤ 反注册
	CloudiskConsul::deregisterService("192.168.149.128", 30500, serviceID);

	return 0;
}
