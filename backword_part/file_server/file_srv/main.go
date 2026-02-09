package main

import (
	"context"
	"fmt"
	filepb "go_test/backword_part/file_server/file_srv/protobuf"
	"go_test/backword_part/log"
	"go_test/internal"
	"net"
	"os"
	"os/signal"
	"syscall"

	"go.uber.org/zap"
	"google.golang.org/grpc"
)

// ElegantExit 优雅退出

func ElegantExit(s *grpc.Server, lis net.Listener) {
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGINT, syscall.SIGTERM)

	sig := <-ch
	log.Logger.Info("received signal, shutting down", zap.String("signal", sig.String()))

	// 1) 停止接收新连接并等待在途请求完成
	s.GracefulStop()

	// 2) 关闭 listener
	_ = lis.Close()
	internal.DeregisterService("consul退出")
	log.Logger.Info("shutdown complete")
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
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	ip := internal.ViperConf.ConsulConfig.FileSrv.Host
	// ---- 1. 获取随机端口并监听（只监听一次！）----
	lis, port, err := ListenAutoPort()
	if err != nil {
		panic(err)
	}

	addr := fmt.Sprintf("%v:%d", ip, port)
	// ---- 2. 创建 gRPC Server ----
	grpcServer := grpc.NewServer()
	// 监听退出信号
	go func() {
		ElegantExit(grpcServer, lis)
	}()
	// consul注册服务
	filepb.RegisterFileServiceServer(grpcServer, &filepb.FileServer{})

	// 注册健康检查
	internal.RegisterGRPCHealth(grpcServer)

	// ---- 3. 注册到 Consul（用 port，不要重复 Listen）----
	err = internal.RegisterGrpcService(
		"file_srv",
		"file_srv_1",
		ip,
		port)
	if err != nil {
		log.Logger.Error(err.Error())
		return
	}

	// ---- 4. 开启kafka消费者-----
	internal.InitKafkaProducer()
	dispatcher := NewOutboxDispatcher(internal.DB, internal.KafkaProducer)
	go dispatcher.Start(ctx)
	log.Logger.Info("kafka producer is running")

	// ---- 5. 直接 Serve(lis)，不要再次 net.Listen！----
	log.Logger.Info("gRPC File Service running on " + addr)
	if err := grpcServer.Serve(lis); err != nil {
		panic(err)
	}

}
