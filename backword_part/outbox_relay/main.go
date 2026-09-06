package main

import (
	"context"
	"go_test/internal"
	"go_test/internal/outboxrelay"
	"os"
	"os/signal"
	"syscall"

	"go.uber.org/zap"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	internal.InitKafkaProducer()
	dispatcher := outboxrelay.NewDispatcher(internal.DB, internal.KafkaProducer)

	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-ch
		internal.Logger.Info("outbox_relay received signal", zap.String("signal", sig.String()))
		cancel()
	}()

	internal.Logger.Info("outbox_relay started")
	dispatcher.Start(ctx)
}
