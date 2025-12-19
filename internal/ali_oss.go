package internal

import (
	"bytes"
	"fmt"
	"net/http"

	"github.com/aliyun/aliyun-oss-go-sdk/oss"
)

type Alioss struct {
	Endpoint        string `mapstructure:"endpoint"`
	AccessKeyId     string `mapstructure:"access_key_id"`
	AccessKeySecret string `mapstructure:"access_key_secret"`
	BucketName      string `mapstructure:"bucket_name"`
}
type OSSClient struct {
	Client *oss.Client
	Bucket *oss.Bucket
}

var OssClient *OSSClient

func NewOSSClient(endpoint, accessKeyID, accessKeySecret, bucketName string) (*OSSClient, error) {
	client, err := oss.New(endpoint, accessKeyID, accessKeySecret)
	if err != nil {
		return nil, err
	}
	bucket, err := client.Bucket(bucketName)
	if err != nil {
		return nil, err
	}
	return &OSSClient{Client: client, Bucket: bucket}, nil
}

// UploadFile 将本地路径localPath的文件上传到OSS指定对象键objectKey
func (c *OSSClient) UploadFile(localPath string, objectKey string) error {
	return c.Bucket.UploadFile(objectKey, localPath, 1*1024*1024,
		oss.Routines(3), oss.Checkpoint(true, "./checkpoint"))
}

// UploadBytes 将内存中的字节数据data上传为OSS上的对象objectKey
func (c *OSSClient) UploadBytes(data []byte, objectKey string) error {
	reader := bytes.NewReader(data)
	return c.Bucket.PutObject(objectKey, reader)
}

// ObjectExists 判断OSS桶中是否存在指定的对象
func (c *OSSClient) ObjectExists(objectKey string) (bool, error) {
	return c.Bucket.IsObjectExist(objectKey)
}

// DeleteObject 删除OSS桶中的指定对象
func (c *OSSClient) DeleteObject(objectKey string) error {
	return c.Bucket.DeleteObject(objectKey)
}

// GetObjectMeta 获取OSS中指定对象的元信息（HTTP头形式）
func (c *OSSClient) GetObjectMeta(objectKey string) (http.Header, error) {
	return c.Bucket.GetObjectMeta(objectKey)
}

// DownloadFile 将OSS中指定对象下载到本地文件localPath
func (c *OSSClient) DownloadFile(objectKey string, localPath string) error {
	return c.Bucket.DownloadFile(objectKey, localPath, 1*1024*1024,
		oss.Routines(3), oss.Checkpoint(true, "./checkpoint"))
}

func InitAli() {
	c, err := NewOSSClient(ViperConf.Alioss.Endpoint, ViperConf.Alioss.AccessKeyId, ViperConf.Alioss.AccessKeySecret, ViperConf.Alioss.BucketName)
	if err != nil {
		panic(fmt.Sprintf("阿里oss客户端创建失败: %v", err))
	}
	OssClient = c
}
