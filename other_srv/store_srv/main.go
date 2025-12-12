package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"runtime"
	"store_srv/kafka"
	"syscall"
)

func main() {

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Kafka configuration
	kafkaConfig := kafka.KafkaConfig{
		Brokers:     []string{"localhost:9092"}, // Replace with your Kafka broker address
		Topic:       "alioss",                   // Topic matching the producer
		GroupID:     "store_srv_group",          // Replace with your consumer group ID
		WorkerCount: runtime.NumCPU() * 4,       // High concurrency for file uploads
	}

	// Start Kafka consumer in a goroutine
	errCh := make(chan error, 1)
	go func() {
		if err := kafka.StartConsumer(ctx, kafkaConfig); err != nil && ctx.Err() == nil {
			errCh <- err
		}
	}()

	// Wait for interrupt signal to gracefully shutdown the server
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	select {
	case <-quit:
		log.Println("Shutting down server...")
	case err := <-errCh:
		log.Printf("Kafka consumer stopped with error: %v", err)
	}
	cancel()
}
