package internal

import (
	"fmt"
	"go_test/other_srv/store_srv/log"
	"math/rand"
	"sync"

	"github.com/hashicorp/consul/api"

	"google.golang.org/grpc"
	"google.golang.org/grpc/health"
	"google.golang.org/grpc/health/grpc_health_v1"
)

// ConsulServiceConfig 服务注册结构体
type ConsulServiceConfig struct {
	Host string `mapstructure:"host"`
	Port int    `mapstructure:"port"`
}

type ConsulConfig struct {
	Host       string              `mapstructure:"host"`
	Port       string              `mapstructure:"port"`
	AccountSrv ConsulServiceConfig `mapstructure:"account_srv"`
	FileSrv    ConsulServiceConfig `mapstructure:"file_srv"`
	MCPSrv     ConsulServiceConfig `mapstructure:"mcp_srv"`
}

var (
	ConsulClient *api.Client
	once         sync.Once
)

// InitConsul 初始化Concul客户端
func InitConsul() {
	once.Do(func() {
		config := api.DefaultConfig()
		config.Address = fmt.Sprintf(
			"%s:%s",
			ViperConf.ConsulConfig.Host,
			ViperConf.ConsulConfig.Port,
		)

		client, err := api.NewClient(config)
		if err != nil {
			log.Logger.Error("consul init fail:" + err.Error())
			return
		}
		ConsulClient = client
	})
}

// RegisterGrpcService Grpc注册服务
func RegisterGrpcService(serviceName, serviceID, host string, port int) error {

	// 健康检查配置
	check := &api.AgentServiceCheck{
		GRPC:     fmt.Sprintf("%s:%d", host, port), // gRPC 健康检查接口
		Interval: "5s",
		Timeout:  "3s",

		// 服务 30 秒不健康 → 自动注销
		DeregisterCriticalServiceAfter: "30s",
	}

	registration := &api.AgentServiceRegistration{
		ID:      serviceID,
		Name:    serviceName,
		Address: host,
		Port:    port,
		Tags:    []string{"grpc", "v1"},
		Check:   check,
	}

	// 注册服务
	if err := ConsulClient.Agent().
		ServiceRegister(registration); err != nil {
		log.Logger.Error("register service fail:" + err.Error())
		return err
	}
	log.Logger.Info(fmt.Sprintf("服务注册成功: %s (%s:%d)\n",
		serviceName, host, port))
	return nil
}

// RegisterGRPCHealth GRPC健康
func RegisterGRPCHealth(grpcServer *grpc.Server) {
	hs := health.NewServer()
	grpc_health_v1.RegisterHealthServer(grpcServer, hs)
	hs.SetServingStatus("",
		grpc_health_v1.HealthCheckResponse_SERVING)
}

// RegisterGinService Gin注册服务
func RegisterGinService(serviceName, serviceID, host string, port int) error {

	// 健康检查配置
	check := &api.AgentServiceCheck{
		HTTP:                           fmt.Sprintf("http://%s:%d/health", host, port),
		Interval:                       "5s",
		Timeout:                        "3s",
		DeregisterCriticalServiceAfter: "30s",
	}

	registration := &api.AgentServiceRegistration{
		ID:      serviceID,
		Name:    serviceName,
		Address: host,
		Port:    port,
		Tags:    []string{"http", "v1"},
		Check:   check,
	}

	// 注册服务
	if err := ConsulClient.Agent().
		ServiceRegister(registration); err != nil {
		log.Logger.Error("register service fail:" + err.Error())
		return err
	}
	log.Logger.Info(fmt.Sprintf("服务注册成功: %s (%s:%d)\n",
		serviceName, host, port))
	return nil
}

// DeregisterService 服务注销（在程序退出时执行）
// 例如：defer consul.DeregisterService("account_web_1")
func DeregisterService(serviceID string) {
	if ConsulClient != nil {
		err := ConsulClient.Agent().ServiceDeregister(serviceID)
		if err != nil {
			log.Logger.Error(fmt.Sprintf("服务注销失败:%v", err.Error()))
			return
		}
		log.Logger.Info(fmt.Sprintf("服务已注销:%v", serviceID))
	}
}

// DiscoverService 服务发现
func DiscoverService(serviceName string) (string, int, error) {

	// 只查询健康服务
	services, _, err :=
		ConsulClient.Health().Service(serviceName, "", true, nil)

	if err != nil {
		log.Logger.Error(fmt.Sprintf("服务发现失败:%v", err.Error()))
		return "", 0, err
	}

	if len(services) == 0 {
		return "", 0, fmt.Errorf("未找到可用服务: %s", serviceName)
	}

	// 简单负载均衡：随机选一个
	srv := services[rand.Intn(len(services))]

	host := srv.Service.Address
	port := srv.Service.Port

	return host, port, nil
}
