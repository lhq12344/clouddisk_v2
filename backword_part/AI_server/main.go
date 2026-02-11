package main

import (
	"fmt"
	aipb "go_test/backword_part/AI_server/protobuf"
	"go_test/backword_part/log"
	"go_test/internal"
	"net"
	"os"
	"os/signal"
	"syscall"

	"google.golang.org/grpc"
)

// ElegantExit 优雅退出
func ElegantExit() {
	exit := make(chan os.Signal, 1)
	signal.Notify(exit, syscall.SIGINT, syscall.SIGTERM)
	<-exit
	internal.DeregisterService("consul退出")
	fmt.Println("主协程退出")
}

// ListenAutoPort AutoPort 自动寻找空闲的port
func ListenAutoPort() (net.Listener, int, error) {
	ln, err := net.Listen("tcp", ":0")
	if err != nil {
		return nil, 0, err
	}
	port := ln.Addr().(*net.TCPAddr).Port
	return ln, port, nil
}

func main() {
	ip := internal.ViperConf.ConsulConfig.AccountSrv.Host
	// ---- 1. 获取随机端口并监听（只监听一次！）----
	lis, port, err := ListenAutoPort()
	if err != nil {
		panic(err)
	}

	addr := fmt.Sprintf("%v:%d", ip, port)
	// ---- 2. 创建 gRPC Server ----
	grpcServer := grpc.NewServer()

	// consul注册服务
	aipb.RegisterAIServiceServer(grpcServer, &aipb.AIServer{})

	// 注册健康检查
	internal.RegisterGRPCHealth(grpcServer)

	// ---- 3. 注册到 Consul（用 port，不要重复 Listen）----
	err = internal.RegisterGrpcService(
		"AI_srv",
		"AI_srv_1",
		ip,
		port)
	if err != nil {
		log.Logger.Error("account_srv 注册失败")
		return
	}

	log.Logger.Info("gRPC Account Service running on " + addr)

	// ---- 4. 直接 Serve(lis)，不要再次 net.Listen！----
	if err := grpcServer.Serve(lis); err != nil {
		panic(err)
	}

	// 优雅退出
	ElegantExit()
}
