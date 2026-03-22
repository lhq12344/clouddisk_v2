package internal

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"

	"github.com/IBM/sarama"
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

	producer, err := sarama.NewSyncProducer(brokers, cfg)
	if err != nil {
		panic(err)
	}
	KafkaProducer = producer
	Logger.Info("[InitKafkaProducer]Kafka producer created")
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
