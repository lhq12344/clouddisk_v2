package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"store_srv/kafka"
	"syscall"
)

func main() {

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Kafka configuration
	kafkaConfig := kafka.KafkaConfig{
		Brokers: []string{"localhost:9092"}, // Replace with your Kafka broker address
		Topic:   "alioss",                   // Topic matching the producer
		GroupID: "store_srv_group",          // Replace with your consumer group ID
	}

	// Start Kafka consumer in a goroutine
	go kafka.StartConsumer(ctx, kafkaConfig)

	// Wait for interrupt signal to gracefully shutdown the server
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit
	log.Println("Shutting down server...")
}
