package internal

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/log"

	"github.com/IBM/sarama"
)

const Topic = "alioss"

type KafkaConfig struct {
	Host string `mapstructure:"host"`
	Port string `mapstructure:"port"`
}

type FileUploadMsg struct {
	Code    int    `json:"code"`
	Sha1    string `json:"sha1"`
	Size    int64  `json:"size"`
	Content []byte `json:"content"`
	Type    string `json:"type"`
}

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
	log.Logger.Info("[InitKafkaProducer]Kafka producer created")
	return
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
		Key:   sarama.StringEncoder(msg.Sha1), // 用 sha1 做 key，方便按文件分区
		Value: sarama.ByteEncoder(payload),
	}

	_, _, err = KafkaProducer.SendMessage(kafkaMsg)
	return err
}
