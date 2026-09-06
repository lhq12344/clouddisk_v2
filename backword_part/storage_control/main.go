package main

import (
	"fmt"
	storagecontrolpb "go_test/clouddisk_v2/storage_control/protobuf"
	"go_test/internal"
	"net"
	"os"
	"os/signal"
	"syscall"

	"go.uber.org/zap"
	"google.golang.org/grpc"
)

func listenAutoPort() (net.Listener, int, error) {
	ln, err := net.Listen("tcp", ":0")
	if err != nil {
		return nil, 0, err
	}
	return ln, ln.Addr().(*net.TCPAddr).Port, nil
}

func gracefulExit(server *grpc.Server, lis net.Listener, serviceID string) {
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGINT, syscall.SIGTERM)
	sig := <-ch
	internal.Logger.Info("storage-control received signal", zap.String("signal", sig.String()))
	server.GracefulStop()
	_ = lis.Close()
	internal.DeregisterService(serviceID)
	internal.Logger.Info("storage-control shutdown complete")
}

func main() {
	host := os.Getenv("STORAGE_CONTROL_HOST")
	if host == "" {
		host = internal.ViperConf.ConsulConfig.StorageControl.Host
	}
	if host == "" {
		host = "127.0.0.1"
	}

	lis, port, err := listenAutoPort()
	if err != nil {
		panic(err)
	}
	serviceID := fmt.Sprintf("storage_control_%d", port)

	grpcServer := grpc.NewServer(
		grpc.UnaryInterceptor(internal.RequestIDUnaryInterceptor()),
		grpc.StreamInterceptor(internal.RequestIDStreamInterceptor()),
	)
	storagecontrolpb.RegisterStorageControlServer(grpcServer, &storagecontrolpb.StorageControlService{})
	internal.RegisterGRPCHealth(grpcServer)

	go gracefulExit(grpcServer, lis, serviceID)

	if err := internal.RegisterGrpcService("storage_control", serviceID, host, port); err != nil {
		internal.Logger.Error("storage-control registration failed", zap.Error(err))
		return
	}

	addr := fmt.Sprintf("%s:%d", host, port)
	internal.Logger.Info("gRPC Storage Control running on " + addr)
	if err := grpcServer.Serve(lis); err != nil {
		panic(err)
	}
}
