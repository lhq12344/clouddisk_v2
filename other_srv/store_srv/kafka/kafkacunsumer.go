package kafka

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"strconv"
	"sync"

	"github.com/IBM/sarama"
	"go.uber.org/zap"

	"store_srv/ali_oss"
	"store_srv/localfile"
	"store_srv/log"
)

// KafkaConfig holds the required configuration for starting a consumer group.
type KafkaConfig struct {
	Brokers     []string
	Topic       string
	GroupID     string
	WorkerCount int
}

// FileMessage 定义Kafka消息的结构体，用于反序列化
type FileMessage struct {
	Sha1    string `json:"sha1"`
	Size    int64  `json:"size"`
	Content []byte `json:"content"` // 假设content以字节数组形式传递
}

// FileUploadConsumer 文件上传消费者，实现在ConsumerGroupHandler接口
type FileUploadConsumer struct {
	ossClient   *ali_oss.OSSClient // OSS对象，用于oss操作
	workerCount int                // 协程池大小
	taskCh      chan *sarama.ConsumerMessage
	wg          sync.WaitGroup
	localFile   *localfile.LoadFile
}

// currentSession 用于保存当前ConsumerGroupSession，在并发处理中用于标记消息
var currentSession sarama.ConsumerGroupSession

// NewFileUploadConsumer 构造函数，创建消费者处理实例并初始化协程池
func NewFileUploadConsumer(ctx context.Context, workerCount int) *FileUploadConsumer {
	accessKeyID := os.Getenv("ALIYUN_ACCESS_KEY_ID")
	accessKeySecret := os.Getenv("ALIYUN_ACCESS_KEY_SECRET")
	ossClient, err := ali_oss.NewOSSClient(ali_oss.Endpoint, accessKeyID, accessKeySecret, ali_oss.BucketName)
	if err != nil {
		log.Logger.Info("oss connect fail", zap.String("err", err.Error()))
		return nil
	}

	c := &FileUploadConsumer{
		ossClient:   ossClient,
		workerCount: workerCount,
		taskCh:      make(chan *sarama.ConsumerMessage, workerCount*2), // 缓冲队列
		localFile:   &localfile.LoadFile{},
	}
	for i := 0; i < workerCount; i++ {
		c.wg.Add(1)
		go func(ctx context.Context) {
			defer c.wg.Done()
			for {
				select {
				case msg, ok := <-c.taskCh:
					if !ok {
						return
					}
					c.processMessage(msg)
					if currentSession != nil {
						currentSession.MarkMessage(msg, "")
					}
				case <-ctx.Done():
					return
				}
			}
		}(ctx)
	}
	return c
}

// Setup 在新的会话开始时调用（如平衡分区后）
func (c *FileUploadConsumer) Setup(session sarama.ConsumerGroupSession) error {
	currentSession = session
	log.Logger.Info("Consumer session setup")
	return nil
}

// Cleanup 在会话结束时调用（如发生再均衡前）
func (c *FileUploadConsumer) Cleanup(session sarama.ConsumerGroupSession) error {
	close(c.taskCh)
	c.wg.Wait()
	log.Logger.Info("Consumer session cleanup, all pending tasks done.")
	return nil
}

// ConsumeClaim 消费组对指定Topic/分区的消费逻辑
func (c *FileUploadConsumer) ConsumeClaim(session sarama.ConsumerGroupSession, claim sarama.ConsumerGroupClaim) error {
	for {
		select {
		case msg := <-claim.Messages():
			if msg == nil {
				continue
			}
			c.taskCh <- msg
		case <-session.Context().Done():
			return nil
		}
	}
}

// 实际处理单条消息的逻辑，包括反序列化、缓存检查和OSS上传
func (c *FileUploadConsumer) processMessage(msg *sarama.ConsumerMessage) {
	var fileMsg FileMessage
	if err := json.Unmarshal(msg.Value, &fileMsg); err != nil {
		log.Logger.Info("Failed to decode message ",
			zap.String("offset", strconv.FormatInt(msg.Offset, 10)),
			zap.String("error", err.Error()))
		return
	}
	filePath := c.localFile.PathForSha1(fileMsg.Sha1)
	fileExists := c.localFile.FileExists(filePath)
	ossExists, err := c.ossClient.ObjectExists(fileMsg.Sha1)
	if err != nil {
		log.Logger.Info("OSS existence check failed ",
			zap.String("sha1", fileMsg.Sha1),
			zap.String("error", err.Error()))
	}
	if fileExists && ossExists {
		log.Logger.Info("File %s already in cache and OSS", zap.String("sha1", fileMsg.Sha1))
		return
	}
	if !fileExists {
		_, err := c.localFile.SaveFile(fileMsg.Sha1, fileMsg.Content)
		if err != nil {
			log.Logger.Info("Failed to write file to cache",
				zap.String("filepath", filePath),
				zap.String("error", err.Error()))
			return
		}
	}
	if !ossExists {
		if err := c.ossClient.UploadFile(filePath, fileMsg.Sha1); err != nil {
			log.Logger.Info("Failed to upload to OSS",
				zap.String("filepath", filePath),
				zap.String("error", err.Error()))
			return
		}
		log.Logger.Info("Uploaded file to OSS successfully",
			zap.String("fileSha", fileMsg.Sha1),
			zap.String("size", strconv.FormatInt(fileMsg.Size, 10)))
	} else {
		log.Logger.Info("OSS already has file, local cache updated (if needed).",
			zap.String("fileSha", fileMsg.Sha1))
	}
}

// StartConsumer initializes the consumer group and starts consuming with the provided config.
func StartConsumer(ctx context.Context, cfg KafkaConfig) error {
	if len(cfg.Brokers) == 0 {
		return fmt.Errorf("kafka brokers must be provided")
	}
	if cfg.Topic == "" {
		return fmt.Errorf("kafka topic must be provided")
	}
	if cfg.GroupID == "" {
		return fmt.Errorf("kafka group id must be provided")
	}

	workerCount := cfg.WorkerCount
	if workerCount <= 0 {
		workerCount = 1
	}

	consumer := NewFileUploadConsumer(ctx, workerCount)
	if consumer == nil {
		return fmt.Errorf("failed to create file upload consumer")
	}

	saramaCfg := sarama.NewConfig()
	saramaCfg.Version = sarama.V2_8_0_0
	saramaCfg.Consumer.Offsets.Initial = sarama.OffsetNewest
	saramaCfg.Consumer.Return.Errors = true

	group, err := sarama.NewConsumerGroup(cfg.Brokers, cfg.GroupID, saramaCfg)
	if err != nil {
		log.Logger.Error("Failed to create consumer group", zap.Error(err))
		return err
	}
	defer func() {
		if err := group.Close(); err != nil {
			log.Logger.Error("Failed to close consumer group", zap.Error(err))
		}
	}()

	go func() {
		for err := range group.Errors() {
			log.Logger.Error("Kafka consumer group error", zap.Error(err))
		}
	}()

	for {
		if ctx.Err() != nil {
			return ctx.Err()
		}

		if err := group.Consume(ctx, []string{cfg.Topic}, consumer); err != nil {
			log.Logger.Error("Error from consumer", zap.Error(err))
		}

		if ctx.Err() != nil {
			return ctx.Err()
		}
	}
}
