package kafka

import (
	"context"
	"encoding/json"
	"os"
	"strconv"
	"sync"

	"github.com/IBM/sarama"
	"go.uber.org/zap"

	"store_srv/ali_oss"
	"store_srv/localfile"
	"store_srv/log"
)

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
	cancelFunc  context.CancelFunc
	mu          sync.Mutex
}

// NewFileUploadConsumer 构造函数，创建消费者处理实例并初始化协程池
func NewFileUploadConsumer(workerCount int) *FileUploadConsumer {
	//oss
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
	}
	return c
}

// Setup 在新的会话开始时调用（如平衡分区后）
func (c *FileUploadConsumer) Setup(session sarama.ConsumerGroupSession) error {
	c.mu.Lock()
	defer c.mu.Unlock()

	taskCh := make(chan *sarama.ConsumerMessage, c.workerCount*2) // 缓冲队列
	c.taskCh = taskCh

	workerCtx, cancel := context.WithCancel(context.Background())
	c.cancelFunc = cancel

	// 启动 workerCount 个后台协程从队列中消费任务
	for i := 0; i < c.workerCount; i++ {
		c.wg.Add(1)
		go func(ctx context.Context, s sarama.ConsumerGroupSession) {
			defer c.wg.Done()
			for {
				select {
				case <-ctx.Done():
					return
				case msg, ok := <-taskCh:
					if !ok {
						return
					}
					// 处理消息并上传OSS
					c.processMessage(msg)
					// 标记消息已处理，用于提交偏移量
					// （MarkMessage是并发安全的，可在协程中调用）
					// 注意：只有在确认消息处理完成且无需重试时才标记
					// 若处理失败可选择不标记以使Kafka稍后重新投递
					s.MarkMessage(msg, "")
				}
			}
		}(workerCtx, session)
	}
	log.Logger.Info("Consumer session setup")
	return nil
}

// Cleanup 在会话结束时调用（如发生再均衡前）
func (c *FileUploadConsumer) Cleanup(session sarama.ConsumerGroupSession) error {
	c.mu.Lock()
	if c.cancelFunc != nil {
		c.cancelFunc()
		c.cancelFunc = nil
	}
	// 关闭任务通道，停止接受新任务
	if c.taskCh != nil {
		close(c.taskCh)
		c.taskCh = nil
	}
	c.mu.Unlock()
	// 等待正在处理的任务完成
	c.wg.Wait()
	log.Logger.Info("Consumer session cleanup, all pending tasks done.")
	return nil
}

// ConsumeClaim 消费组对指定Topic/分区的消费逻辑
func (c *FileUploadConsumer) ConsumeClaim(session sarama.ConsumerGroupSession, claim sarama.ConsumerGroupClaim) error {
	// 从Messages()通道持续读取消息
	for {
		select {
		case msg := <-claim.Messages():
			if msg == nil {
				continue
			}
			// 将消息指针投递到任务队列，由后台协程并发处理
			c.taskCh <- msg
			// 可以选择不在此立即MarkMessage，由后台处理完成后Mark
			// 此处不立即Mark可确保任务真正完成后再提交offset，保证至少处理一次
		case <-session.Context().Done():
			// 分区消费会话结束（可能触发rebalance），跳出循环
			return nil
		}
	}
}

// 实际处理单条消息的逻辑，包括反序列化、缓存检查和OSS上传
func (c *FileUploadConsumer) processMessage(msg *sarama.ConsumerMessage) {
	// 反序列化消息内容
	var fileMsg FileMessage
	if err := json.Unmarshal(msg.Value, &fileMsg); err != nil {
		log.Logger.Info("Failed to decode message ",
			zap.String("offset", strconv.FormatInt(msg.Offset, 10)),
			zap.String("error", err.Error()))
		return
	}
	// 构造本地缓存文件路径（使用文件sha1作为文件名）
	filePath := c.localFile.PathForSha1(fileMsg.Sha1)
	// 检查本地缓存是否已有该文件
	fileExists := c.localFile.FileExists(filePath)
	// 检查OSS中是否已存在该对象
	ossExists, err := c.ossClient.ObjectExists(fileMsg.Sha1)
	if err != nil {
		log.Logger.Info("OSS existence check failed ",
			zap.String("sha1", fileMsg.Sha1),
			zap.String("error", err.Error()))
		// 如果无法检查OSS，可选择不跳过上传
	}
	// 判断跳过条件：本地和OSS均已存在则跳过上传
	if fileExists && ossExists {
		log.Logger.Info("File %s already in cache and OSS", zap.String("sha1", fileMsg.Sha1))
		return
	}
	// 如果本地不存在则先将内容写入本地文件缓存
	if !fileExists {
		_, err := c.localFile.SaveFile(fileMsg.Sha1, fileMsg.Content)
		if err != nil {
			log.Logger.Info("Failed to write file to cache",
				zap.String("filepath", filePath),
				zap.String("error", err.Error()))
			return
		}
	}
	// 如果OSS不存在对应文件，则执行上传
	if !ossExists {
		if err := c.ossClient.UploadFile(filePath, fileMsg.Sha1); err != nil {
			log.Logger.Info("Failed to upload to OSS",
				zap.String("filepath", filePath),
				zap.String("error", err.Error()))
			// 上传失败，此处可根据需要选择重试或不MarkMessage以触发重消费
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
