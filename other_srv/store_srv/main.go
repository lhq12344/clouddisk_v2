package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"store_srv/kafka"
	"syscall"
	"time"

	"github.com/IBM/sarama"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// 捕获退出信号，确保消费者能够优雅退出并提交已处理的偏移量
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-stop
		cancel()
	}()

	// Kafka consumer group 配置
	cfg := sarama.NewConfig()
	cfg.Version = sarama.V3_5_0_0
	cfg.Consumer.Group.Rebalance.Strategy = sarama.BalanceStrategyRange
	cfg.Consumer.Offsets.Initial = sarama.OffsetNewest
	cfg.Consumer.Offsets.AutoCommit.Enable = true
	cfg.Consumer.Offsets.AutoCommit.Interval = 1 * time.Second
	cfg.Consumer.Return.Errors = true

	brokers := []string{"192.168.149.128:31092"}
	groupID := "store_srv_group"
	topics := []string{"file.upload.cmd"}

	consumerGroup, err := sarama.NewConsumerGroup(brokers, groupID, cfg)
	if err != nil {
		log.Fatalf("failed to create consumer group: %v", err)
	}
	defer consumerGroup.Close()

	// 后台监听消费错误，便于监控和告警
	go func() {
		for err := range consumerGroup.Errors() {
			log.Printf("consumer group error: %v", err)
		}
	}()

	handler := kafka.NewFileUploadConsumer(ctx, 10)
	if handler == nil {
		log.Fatalf("failed to initialize file upload consumer")
		return
	}

	// 持续消费，Consume 会在 Rebalance 后重新返回，需要循环调用以保持消费
	for {
		if err := consumerGroup.Consume(ctx, topics, handler); err != nil {
			log.Printf("consume error: %v", err)
		}

		if ctx.Err() != nil {
			// 上下文被取消，正常退出
			break
		}
	}
}
