package main

import (
	"context"
	"go_test/other_srv/store_srv/kafka"
	"log"
	"os"
	"os/signal"
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
		log.Println("Received shutdown signal, gracefully shutting down...")
		cancel()
	}()

	brokers := []string{"127.0.0.1:31092"}
	mainTopic := "file.upload.cmd"
	dlqTopic := "file.upload.cmd.dlq"
	mainGroupID := "store_srv_group"
	dlqGroupID := "store_srv_dlq_group"

	// 1. 创建 DLQ Producer
	dlqProducer, err := kafka.NewDLQProducer(brokers, dlqTopic)
	if err != nil {
		log.Fatalf("failed to create DLQ producer: %v", err)
	}
	defer dlqProducer.Close()
	log.Println("DLQ producer created successfully")

	// 2. 创建主消费者（带 DLQ 支持）
	cfg := sarama.NewConfig()
	cfg.Version = sarama.V3_5_0_0
	cfg.Consumer.Group.Rebalance.Strategy = sarama.BalanceStrategyRange
	cfg.Consumer.Offsets.Initial = sarama.OffsetNewest
	cfg.Consumer.Offsets.AutoCommit.Enable = true
	cfg.Consumer.Offsets.AutoCommit.Interval = 1 * time.Second
	cfg.Consumer.Return.Errors = true

	consumerGroup, err := sarama.NewConsumerGroup(brokers, mainGroupID, cfg)
	if err != nil {
		log.Fatalf("failed to create consumer group: %v", err)
	}
	defer consumerGroup.Close()

	// 后台监听消费错误
	go func() {
		for err := range consumerGroup.Errors() {
			log.Printf("consumer group error: %v", err)
		}
	}()

	handler := kafka.NewFileUploadConsumer(ctx, 10, dlqProducer)
	if handler == nil {
		log.Fatalf("failed to initialize file upload consumer")
		return
	}
	log.Println("Main consumer created successfully")

	// 3. 创建 DLQ Consumer（低频处理）
	dlqConfig := kafka.DefaultDLQConsumerConfig()
	dlqConsumer, err := kafka.NewDLQConsumer(brokers, dlqGroupID, dlqTopic, handler, dlqConfig)
	if err != nil {
		log.Fatalf("failed to create DLQ consumer: %v", err)
	}
	defer dlqConsumer.Stop()

	// 启动 DLQ 消费者（后台运行）
	go dlqConsumer.Start()
	log.Printf("DLQ consumer started (poll interval: %v)", dlqConfig.PollInterval)

	// 4. 创建 DLQ Manager（用于管理 API）
	dlqManager := kafka.NewDLQManager(handler)
	log.Println("DLQ manager created successfully")

	// 5. 定期清理旧的已解决消息（可选）
	go func() {
		ticker := time.NewTicker(24 * time.Hour)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				deleted, err := dlqManager.CleanupOldMessages(ctx, 30) // 保留 30 天
				if err != nil {
					log.Printf("failed to cleanup old DLQ messages: %v", err)
				} else {
					log.Printf("cleaned up %d old DLQ messages", deleted)
				}
			}
		}
	}()

	// 6. 定期打印 DLQ 指标（可选）
	go func() {
		ticker := time.NewTicker(5 * time.Minute)
		defer ticker.Stop()

		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				metrics, err := dlqConsumer.GetMetrics(ctx)
				if err != nil {
					log.Printf("failed to get DLQ metrics: %v", err)
				} else {
					log.Printf("DLQ Metrics: Total=%d, Pending=%d, Retrying=%d, Failed=%d, Resolved=%d",
						metrics.DLQMessageCount,
						metrics.DLQPendingCount,
						metrics.DLQRetryingCount,
						metrics.DLQFailedCount,
						metrics.DLQResolvedCount)
				}
			}
		}
	}()

	log.Println("Store service started successfully")
	log.Printf("Main topic: %s, DLQ topic: %s", mainTopic, dlqTopic)

	// 持续消费主队列
	topics := []string{mainTopic}
	for {
		if err := consumerGroup.Consume(ctx, topics, handler); err != nil {
			log.Printf("consume error: %v", err)
		}

		if ctx.Err() != nil {
			// 上下文被取消，正常退出
			break
		}
	}

	log.Println("Store service stopped gracefully")
}

