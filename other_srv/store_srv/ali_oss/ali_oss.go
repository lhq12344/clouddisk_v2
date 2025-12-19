package ali_oss

import (
	"bytes"
	"net/http"

	"github.com/aliyun/aliyun-oss-go-sdk/oss"
)

type OSSClient struct {
	Client *oss.Client
	Bucket *oss.Bucket
}

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
func (c *OSSClient) UploadFile(localPath string, objectKey string, contentType string) error {
	return c.Bucket.UploadFile(objectKey, localPath, 1*1024*1024,
		oss.Routines(3), oss.Checkpoint(true, "./checkpoint"), oss.ContentType(contentType))
}

// UploadBytes 将内存中的字节数据data上传为OSS上的对象objectKey
func (c *OSSClient) UploadBytes(data []byte, objectKey string, contentType string) error {
	reader := bytes.NewReader(data)
	return c.Bucket.PutObject(objectKey, reader, oss.Routines(3), oss.Checkpoint(true, "./checkpoint"), oss.ContentType(contentType))
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
