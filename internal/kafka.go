package internal

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	"time"

	"github.com/IBM/sarama"
	"go.uber.org/zap"
)

const Topic = model.FileUploadEventTopic

type KafkaConfig struct {
	Host string `mapstructure:"host"`
	Port string `mapstructure:"port"`
}

type FileUploadMsg = model.FileEventPayload

var KafkaProducer sarama.SyncProducer

func InitKafkaProducer() {
	brokers := []string{ViperConf.KafkaConfig.Host + ":" + ViperConf.KafkaConfig.Port}
	cfg := sarama.NewConfig()
	cfg.Producer.RequiredAcks = sarama.WaitForAll
	cfg.Producer.Retry.Max = 3
	cfg.Producer.Return.Successes = true // SyncProducer 必须为 true
	cfg.Net.DialTimeout = 5 * time.Second
	cfg.Net.ReadTimeout = 5 * time.Second
	cfg.Net.WriteTimeout = 5 * time.Second

	var (
		producer sarama.SyncProducer
		err      error
	)

	for attempt := 1; attempt <= 15; attempt++ {
		producer, err = sarama.NewSyncProducer(brokers, cfg)
		if err == nil {
			KafkaProducer = producer
			Logger.Info("[InitKafkaProducer]Kafka producer created")
			return
		}

		Logger.Warn("[InitKafkaProducer]kafka producer init retry",
			zap.Int("attempt", attempt),
			zap.Int("max_attempts", 15),
			zap.Strings("brokers", brokers),
			zap.Error(err),
		)

		if attempt < 15 {
			time.Sleep(2 * time.Second)
		}
	}

	panic(err)
}

// ProduceFileUploadMsg 发送消息到 Kafka
func ProduceFileUploadMsg(ctx context.Context, topic string, msg FileUploadMsg) error {
	if KafkaProducer == nil {
		return fmt.Errorf("[ProduceFileUploadMsg]kafka producer not initialized")
	}

	payload, err := json.Marshal(msg)
	if err != nil {
		return err
	}

	kafkaMsg := &sarama.ProducerMessage{
		Topic: topic,
		Key:   sarama.StringEncoder(msg.Sha1),
		Value: sarama.ByteEncoder(payload),
	}

	_, _, err = KafkaProducer.SendMessage(kafkaMsg)
	return err
}
