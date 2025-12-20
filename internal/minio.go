package internal

import (
	"bytes"
	"context"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/minio/minio-go/v7"
	"github.com/minio/minio-go/v7/pkg/credentials"
)

type MinioConf struct {
	Host      string `mapstructure:"host"` // 例如：minio-nodeport.infra:9000 或 192.168.149.128:30900
	Port      int    `mapstructure:"port"`
	AccessKey string `mapstructure:"accessKey"`   // MinIO AccessKey
	SecretKey string `mapstructure:"secretKey"`   // MinIO SecretKey
	Bucket    string `mapstructure:"bucket_name"` // bucket name

}

type MINIOClient struct {
	Client *minio.Client
	Bucket string
}

var MinIOClient *MINIOClient

// normalizeEndpoint: 允许你配置成 "http://x:9000" 或 "x:9000"
func normalizeEndpoint(ep string) (string, bool, error) {
	ep = strings.TrimSpace(ep)
	if ep == "" {
		return "", false, fmt.Errorf("empty endpoint")
	}

	// 如果包含 scheme，解析之
	if strings.HasPrefix(ep, "http://") || strings.HasPrefix(ep, "https://") {
		u, err := url.Parse(ep)
		if err != nil {
			return "", false, err
		}
		host := u.Host
		if host == "" {
			return "", false, fmt.Errorf("invalid endpoint: %s", ep)
		}
		useSSL := (u.Scheme == "https")
		return host, useSSL, nil
	}

	// 默认认为是 host:port
	return ep, false, nil
}

func NewMinIOOSSClient(endpoint, accessKey, secretKey, bucketName string, useSSL bool, region string) (*MINIOClient, error) {
	// endpoint 既支持 "http(s)://host:port" 也支持 "host:port"
	host, inferredSSL, err := normalizeEndpoint(endpoint)
	if err != nil {
		return nil, err
	}
	// 如果 endpoint 自带 scheme，以 scheme 为准；否则用 useSSL 入参
	if strings.HasPrefix(endpoint, "http://") || strings.HasPrefix(endpoint, "https://") {
		useSSL = inferredSSL
	}

	cli, err := minio.New(host, &minio.Options{
		Creds:  credentials.NewStaticV4(accessKey, secretKey, ""),
		Secure: useSSL,
		Region: region, // 可为空
	})
	if err != nil {
		return nil, err
	}

	c := &MINIOClient{Client: cli, Bucket: bucketName}

	// 建议：确保 bucket 存在（幂等）
	ctx := context.Background()
	exists, err := cli.BucketExists(ctx, bucketName)
	if err != nil {
		return nil, err
	}
	if !exists {
		// MinIO 里 region 可以为空；如果你配置了 region，就传进去
		if err := cli.MakeBucket(ctx, bucketName, minio.MakeBucketOptions{Region: region}); err != nil {
			// 并发情况下可能已经被创建，再次确认
			exists2, err2 := cli.BucketExists(ctx, bucketName)
			if err2 != nil || !exists2 {
				return nil, err
			}
		}
	}

	return c, nil
}

// UploadFile 将本地文件上传到 MinIO：objectKey 为对象键
func (c *MINIOClient) MinIOUploadFile(localPath string, objectKey string) error {
	ctx := context.Background()
	_, err := c.Client.FPutObject(ctx, c.Bucket, objectKey, localPath, minio.PutObjectOptions{})
	return err
}

func (c *MINIOClient) MinIOUploadBytes(data []byte, objectKey, contentType string) error {
	ctx := context.Background()
	reader := bytes.NewReader(data)

	if contentType == "" {
		contentType = "application/octet-stream"
	}

	_, err := c.Client.PutObject(ctx, c.Bucket, objectKey, reader, int64(reader.Len()),
		minio.PutObjectOptions{
			ContentType: contentType,
		},
	)
	return err
}

func (c *MINIOClient) MinIOObjectExists(objectKey string) (bool, error) {
	ctx := context.Background()
	_, err := c.Client.StatObject(ctx, c.Bucket, objectKey, minio.StatObjectOptions{})
	if err == nil {
		return true, nil
	}
	// 判断是否是“不存在”
	resp := minio.ToErrorResponse(err)
	if resp.Code == "NoSuchKey" || resp.Code == "NotFound" {
		return false, nil
	}

	return false, err
}

func (c *MINIOClient) MinIODeleteObject(objectKey string) error {
	ctx := context.Background()
	return c.Client.RemoveObject(ctx, c.Bucket, objectKey, minio.RemoveObjectOptions{})
}

func (c *MINIOClient) MinIOGetObjectMeta(objectKey string) (http.Header, error) {
	ctx := context.Background()
	info, err := c.Client.StatObject(ctx, c.Bucket, objectKey, minio.StatObjectOptions{})
	if err != nil {
		return nil, err
	}
	// info.Metadata 是 http.Header
	// 你如果还想要 size/etag/lastModified，可以扩展返回值或另写一个方法
	return info.Metadata, nil
}

func (c *MINIOClient) MinIODownloadFile(objectKey string, localPath string) error {
	ctx := context.Background()
	return c.Client.FGetObject(ctx, c.Bucket, objectKey, localPath, minio.GetObjectOptions{})
}

// PresignPutURL 给前端直传用：PUT 上传 URL
func (c *MINIOClient) MinIOPresignPutURL(objectKey string, expiry time.Duration) (string, error) {
	ctx := context.Background()
	u, err := c.Client.PresignedPutObject(ctx, c.Bucket, objectKey, expiry)
	if err != nil {
		return "", err
	}
	return u.String(), nil
}

// PresignGetURL 给前端下载用：GET 下载 URL
func (c *MINIOClient) MinIOPresignGetURL(objectKey string, expiry time.Duration) (string, error) {
	ctx := context.Background()
	u, err := c.Client.PresignedGetObject(ctx, c.Bucket, objectKey, expiry, nil)
	if err != nil {
		return "", err
	}
	return u.String(), nil
}

func InitMinio() {
	c, err := NewMinIOOSSClient(
		fmt.Sprintf("%s:%d", ViperConf.MinIO.Host, ViperConf.MinIO.Port),
		ViperConf.MinIO.AccessKey,
		ViperConf.MinIO.SecretKey,
		ViperConf.MinIO.Bucket,
		false,
		"us-east-1",
	)
	if err != nil {
		panic(fmt.Sprintf("MinIO 客户端创建失败: %v", err))
	}
	MinIOClient = c
}
