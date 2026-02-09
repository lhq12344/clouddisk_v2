package filepb

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"go_test/backword_part/log"
	"go_test/backword_part/model"
	"go_test/internal"
	"io"
	"mime"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/go-redis/redis/v8"
	"github.com/goccy/go-json"
	"github.com/google/uuid"
	"github.com/minio/minio-go/v7"
	"github.com/pkg/errors"
	"go.uber.org/zap"
	grpc "google.golang.org/grpc"
	codes "google.golang.org/grpc/codes"
	status "google.golang.org/grpc/status"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

const (
	metaKeyPrefix  = "mp:meta:"
	partsKeyPrefix = "mp:parts:"
	defaultTTL     = 24 * time.Hour
)

func genUploadID() string {
	var b [16]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

func metaKey(uploadID string) string  { return metaKeyPrefix + uploadID }
func partsKey(uploadID string) string { return partsKeyPrefix + uploadID }

// buildObjectKey：你可以按“user_id/file_hash”或“user_id/uuid_filename”来。
// 这里优先用 file_hash（利于去重/查找），hash 为空就退化到 upload_id。
func buildObjectKey(userID, fileHash, uploadID, fileName string) string {
	if fileHash != "" {
		return fmt.Sprintf("u/%s/%s", userID, fileHash)
	}
	return fmt.Sprintf("u/%s/%s_%s", userID, uploadID, fileName)
}

// 生成 Content-Disposition（兼容中文文件名）
func buildAttachmentContentDisposition(filename string) string {
	name := strings.ReplaceAll(filename, `"`, "") // 简单去掉引号，避免 header 注入/格式问题
	// RFC 5987：filename* 支持 UTF-8
	return fmt.Sprintf(`attachment; filename="%s"; filename*=UTF-8''%s`,
		name, url.QueryEscape(name))
}

func buildInlineContentDisposition(filename string) string {
	name := strings.TrimSpace(filename)

	// 防止 header 注入（CRLF），以及破坏语法的引号
	name = strings.ReplaceAll(name, "\r", "")
	name = strings.ReplaceAll(name, "\n", "")
	name = strings.ReplaceAll(name, `"`, "")

	// filename* 使用 RFC 5987（UTF-8 + url-escape）
	esc := url.QueryEscape(name)

	return fmt.Sprintf(`inline; filename="%s"; filename*=UTF-8''%s`, name, esc)
}

// 获取文件扩展名
func detectMime(localPath string) string {
	// 先用扩展名（如果本地临时文件带后缀）
	ext := strings.ToLower(filepath.Ext(localPath))
	if ext != "" {
		if mt := mime.TypeByExtension(ext); mt != "" {
			return mt
		}
	}

	// 再用内容嗅探（更可靠）
	f, err := os.Open(localPath)
	if err != nil {
		return "application/octet-stream"
	}
	defer f.Close()

	buf := make([]byte, 512)
	n, _ := f.Read(buf)
	return http.DetectContentType(buf[:n])
}

type FileServer struct {
	UnimplementedFileServiceServer
}

// -------------------------
// 文件基本操作
// -------------------------
func (f FileServer) Filedowm(ctx context.Context, req *ReqFileDown) (*Resp, error) {
	sha1 := req.Filehash
	filename := req.Filename
	userID := req.Userid
	var account model.Account
	// 1) 确认用户
	tx := internal.DB.WithContext(ctx)
	if err := tx.First(&account, userID).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			log.Logger.Error("[Download]account not found", zap.Error(err))
			return nil, fmt.Errorf("[Download]account not found")
		}
		return nil, err
	}
	// 2) 查表确认文件已在oss且状态
	var file model.File
	err := tx.Where("sha1 = ? AND status = ?", sha1, model.FileSuccess).Take(&file).Error
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, fmt.Errorf("[Download]file not found or status mismatch: sha1=%s status=%s", sha1, model.FileSuccess)
		}
		return nil, fmt.Errorf("[Download]db error: %w", err)
	}
	// 3) 生成 OSS 签名下载 URL
	// objectKey：OSS 对象名就是 file/sha1
	objectKey := "files/" + sha1
	reqParams := make(url.Values)
	// Content-Disposition：让浏览器下载时显示原始文件名（带后缀）
	cd := buildAttachmentContentDisposition(filename)
	reqParams.Set("response-content-disposition", cd)
	reqParams.Set("response-content-type", detectMime(filename))
	// 过期时间：按需调整（例如 10 分钟）
	expiry := 10 * time.Minute
	u, err := internal.MinIOClient.Client.PresignedGetObject(ctx, internal.MinIOClient.Bucket, objectKey, expiry, reqParams)
	if err != nil {
		log.Logger.Error("sign url", zap.Error(err))
		return nil, fmt.Errorf("[Download]generate oss signed url failed: %w", err)
	}

	// Code: 1 表示返回的是 OSS 下载 URL
	log.Logger.Info("[Download]download from oss", zap.String("signedUrl", u.String()))
	return &Resp{Code: 0, Message: u.String()}, nil
}

func (f FileServer) LoadFile(ctx context.Context, req *Reqloadfile) (*Resp, error) {
	userID := req.Userid
	fileName := req.Filename
	sha1 := req.FileHash
	content := req.Content // []byte 或 string
	size := req.FileSize

	var resp Resp

	err := internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 1. 校验用户存在
		var account model.Account
		if err := tx.First(&account, userID).Error; err != nil {
			if errors.Is(err, gorm.ErrRecordNotFound) {
				log.Logger.Error("[LoadFile]account not found", zap.Error(err))
				return fmt.Errorf("[LoadFile]account not found")
			}
			log.Logger.Error("[LoadFile]User verification failed", zap.Error(err))
			return err
		}

		// 2. 确保 File 存在：根据 Sha1 去重
		file := model.File{
			Sha1:   sha1,
			Size:   int64(size),
			Status: model.FilePending,
		}

		// 利用唯一索引 + OnConflict，避免重复插入
		if err := tx.Clauses(clause.OnConflict{
			Columns:   []clause.Column{{Name: "sha1"}}, // 以 sha1 为准
			DoNothing: true,
		}).Create(&file).Error; err != nil {
			log.Logger.Error("[LoadFile]File listFile table insertion failed", zap.Error(err))
			return err
		}

		// 如果是 DoNothing，Create 不会把已有记录的 ID 带回，所以再查一次 ID拿到真正的 ID
		if file.ID == 0 {
			if err := tx.Where("sha1 = ?", sha1).First(&file).Error; err != nil {
				log.Logger.Error("[LoadFile]Cannot find the corresponding file", zap.Error(err))
				return err
			}
		}

		// 3. 在 UserFile 中建立关系，同样用唯一索引避免重复
		uf := model.UserFile{
			AccountID: account.ID,
			FileID:    file.ID,
			Name:      fileName,
		}

		if err := tx.Clauses(clause.OnConflict{
			Columns:   []clause.Column{{Name: "account_id"}, {Name: "file_id"}, {Name: "name"}},
			DoNothing: true,
		}).Create(&uf).Error; err != nil {
			log.Logger.Error("[LoadFile]User file relationship creation failed", zap.Error(err))
			return err
		}

		// 关键：如果文件已经 READY说明已经存在oss，不再触发异步写 OSS,到这里结束
		if file.Status == "READY" {
			log.Logger.Info("[LoadFile]The file already exists in the OSS.")
			return nil
		}

		//否则写入outbox发送给kafka生产者线程处理
		txID := uuid.NewString()
		eventID := txID + ":UPLOAD_CMD"
		p := model.UploadCmdPayload{
			TxID:      txID,
			EventID:   eventID,
			FileID:    file.ID,
			Sha1:      sha1,
			Size:      size,
			OssKey:    "files/" + sha1,
			Content:   string(content),
			Type:      detectMime(fileName),
			EventType: model.LoadFile,
		}
		b, _ := json.Marshal(p)
		ob := model.Outbox{
			EventID:     eventID,
			TxID:        txID,
			EventType:   "FILE_UPLOAD_CMD",
			Topic:       "file.upload.cmd",
			Key:         sha1,
			Payload:     string(b),
			Headers:     `{}`,
			Status:      model.OutboxNew,
			RetryCount:  0,
			NextRetryAt: time.Now(),
		}
		if err := tx.Create(&ob).Error; err != nil {
			log.Logger.Error("[LoadFile]Outbox create table failed", zap.Error(err))
			return err
		}

		return nil
	})

	if err != nil {
		log.Logger.Error("[LoadFile]Transaction failed rollback", zap.Error(err))
		return &Resp{Code: 1, Message: err.Error()}, err
	}

	resp = Resp{
		Code:    0,
		Message: "pending",
	}
	return &resp, nil
}

func (f FileServer) Showfile(ctx context.Context, req *Reqshowfile) (*Resp, error) {
	sha1 := req.Filehash
	filename := req.Filename
	userID := req.Userid

	var account model.Account
	tx := internal.DB.WithContext(ctx)
	if err := tx.First(&account, userID).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, fmt.Errorf("[Showfile]account not found")
		}
		return nil, err
	}

	// 查表确认文件已在oss且状态
	var file model.File
	if err := tx.Model(&model.File{}).Where("sha1 = ? AND status=? ", sha1, model.FileSuccess).
		Take(&file).Error; err != nil {
		return nil, fmt.Errorf("[showfile]file not ready in oss: err=%s", err.Error())
	}
	// 生成 OSS inline 预览 URL（关键：inline + filename + content-type）
	//mimeType := detectContentTypeByFilename(filename) // 更推荐从 DB 取 content_type
	objectKey := "files/" + sha1
	reqParams := make(url.Values)
	cd := buildInlineContentDisposition(filename) // inline; filename*=UTF-8''...
	reqParams.Set("response-content-disposition", cd)
	reqParams.Set("response-content-type", detectMime(filename))
	expiry := 10 * time.Minute
	u, err := internal.MinIOClient.Client.PresignedGetObject(ctx, internal.MinIOClient.Bucket, objectKey, expiry, reqParams)
	if err != nil {
		log.Logger.Error("sign url", zap.Error(err))
		return nil, fmt.Errorf("[Showfile]generate oss signed url failed: %w", err)
	}
	log.Logger.Info("[Showfile]showfile from oss", zap.String("signedUrl", u.String()))
	return &Resp{Code: 0, Message: u.String()}, nil
}

func (f FileServer) Filequeryinfo(ctx context.Context, req *ReqFileQuery) (*RespFileQuery, error) {

	userID := req.Userid
	var resp RespFileQuery
	var ufs []model.UserFile
	tx := internal.DB.WithContext(ctx)
	//确认用户存在
	var account model.Account
	if err := tx.First(&account, userID).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, fmt.Errorf("[Showfile]account not found")
		}
		return &RespFileQuery{Code: 1, Message: err.Error()}, err
	}
	//联表查询
	err := tx.Where("account_id = ?", userID).Preload("File").Order("id desc").Find(&ufs).Error
	if err != nil {
		return &RespFileQuery{Code: 1, Message: err.Error()}, err
	}
	//返回
	resp = RespFileQuery{
		Code:    0,
		Message: "db ok ",
		Files:   make([]*FileInfo, 0, len(ufs)), // 预分配
	}
	for _, uf := range ufs {
		fileInfo := FileInfo{
			FileHash:    uf.File.Sha1,
			FileName:    uf.Name,
			FileSizes:   uf.File.Size,
			UploadAt:    uf.File.CreatedAt.String(),
			LastUpdated: uf.File.UpdatedAt.String(),
		}
		resp.Files = append(resp.Files, &fileInfo)
	}
	return &resp, nil
}

func (s FileServer) ResolveFileHash(ctx context.Context, req *ReqResolveFileHash) (*RespResolveFileHash, error) {
	// 1) 参数校验
	filename := strings.TrimSpace(req.GetFilename())
	if filename == "" {
		return &RespResolveFileHash{Code: 400, Message: "filename is required"}, nil
	}

	// 2) userid -> uint（你现在 proto 里 userid 是 string）
	uid64, err := strconv.ParseUint(strings.TrimSpace(req.GetUserid()), 10, 64)
	if err != nil || uid64 == 0 {
		return &RespResolveFileHash{Code: 400, Message: "invalid userid"}, nil
	}
	accountID := uint(uid64)

	// 3) 查询：UserFile(account_id, name) + 预加载 File
	var uf model.UserFile
	err = internal.DB.
		Preload("File").
		Where("account_id = ? AND name = ?", accountID, filename).
		Take(&uf).Error

	if errors.Is(err, gorm.ErrRecordNotFound) {
		return &RespResolveFileHash{Code: 404, Message: "file not found"}, nil
	}
	if err != nil {
		return &RespResolveFileHash{Code: 500, Message: "db error: " + err.Error()}, nil
	}

	// 4) 返回 sha1/size
	return &RespResolveFileHash{
		Code:     200,
		Message:  "ok",
		FileHash: uf.File.Sha1,
		FileSize: uf.File.Size,
	}, nil
}

// -------------------------
// 大文件传输
// -------------------------
func (s FileServer) InitMultipart(ctx context.Context, req *InitReq) (*InitResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil req")
	}
	if req.UserId == "" || req.FileName == "" {
		return nil, status.Error(codes.InvalidArgument, "missing user_id/file_name")
	}
	if req.FileSize <= 0 {
		return nil, status.Error(codes.InvalidArgument, "file_size must be > 0")
	}

	uploadID := genUploadID()
	objectKey := buildObjectKey(req.UserId, req.FileHash, uploadID, req.FileName)

	// 计算 partSize / totalParts（用 minio-go 提供的 OptimalPartInfo）
	// 注意：OptimalPartInfo 文档里说明其默认假设 minPartSize 等常量 :contentReference[oaicite:3]{index=3}
	totalParts, partSize, _, err := minio.OptimalPartInfo(req.FileSize, 0)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "OptimalPartInfo: %v", err)
	}
	if totalParts <= 0 {
		return nil, status.Error(codes.Internal, "invalid total parts")
	}

	// 发起 MinIO Multipart，得到 MinIO 侧 uploadID（后续 PutPart/Complete 都需要这个）
	ossUploadID, err := internal.MinIOClient.MinIOMultipartInit(ctx, objectKey, req.ContentType, map[string]string{
		"x-file-name": req.FileName,
		"x-file-hash": req.FileHash,
		"x-user-id":   req.UserId,
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "minio init multipart: %v", err)
	}

	// 将 meta 写入 Redis（幂等不强求：upload_id 是新生成的）
	mk := metaKey(uploadID)
	pipe := internal.RedisClient.TxPipeline()
	pipe.HSet(ctx, mk, map[string]any{
		"user_id":       req.UserId,
		"file_name":     req.FileName,
		"file_size":     req.FileSize,
		"file_hash":     req.FileHash,
		"content_type":  req.ContentType,
		"object_key":    objectKey,
		"part_size":     partSize,
		"total_parts":   totalParts,
		"oss_upload_id": ossUploadID, // 关键：MinIO 的 uploadID
		"status":        "init",
	})
	pipe.Expire(ctx, mk, defaultTTL)
	pipe.Del(ctx, partsKey(uploadID)) // 清理旧 parts（一般不会存在）
	pipe.Expire(ctx, partsKey(uploadID), defaultTTL)
	if _, err := pipe.Exec(ctx); err != nil {
		_ = internal.MinIOClient.MinIOMultipartAbort(ctx, objectKey, ossUploadID) // 写 redis 失败，尽量回收 MinIO upload
		return nil, status.Errorf(codes.Internal, "redis save meta: %v", err)
	}

	return &InitResp{
		UploadId:   uploadID,
		ObjectKey:  objectKey,
		PartSize:   partSize,
		TotalParts: int32(totalParts),
	}, nil
}

// -------------------- UploadPart（client-stream） --------------------
func (s FileServer) UploadPart(stream grpc.ClientStreamingServer[UploadPartReq, UploadPartResp]) error {
	ctx := stream.Context()

	// 1) 先收 meta（协议约定：第一条是 meta）
	first, err := stream.Recv()
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "recv meta: %v", err)
	}

	meta := first.GetMeta()
	if meta == nil || meta.UploadId == "" || meta.PartNumber <= 0 {
		return status.Error(codes.InvalidArgument, "first message must be meta with upload_id/part_number")
	}

	// 2) 从 Redis 取 object_key / oss_upload_id
	mk := metaKey(meta.UploadId)
	vals, err := internal.RedisClient.HMGet(ctx, mk, "object_key", "oss_upload_id", "content_type", "status").Result()
	if err != nil {
		return status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	objectKey, _ := vals[0].(string)
	ossUploadID, _ := vals[1].(string)
	// contentType, _ := vals[2].(string)
	if objectKey == "" || ossUploadID == "" {
		return status.Error(codes.NotFound, "upload_id not found or expired")
	}

	// 3) 接收 data：你 drogon 那边目前是“meta + data(整块)”，但这里兼容多次 data 分片
	//    为了避免把大块全部堆内存，用临时文件落盘再上传到 MinIO part。
	tmp, err := os.CreateTemp("", "mp-part-*")
	if err != nil {
		return status.Errorf(codes.Internal, "create temp: %v", err)
	}
	defer func() {
		_ = tmp.Close()
		_ = os.Remove(tmp.Name())
	}()

	var written int64
	for {
		in, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			return status.Errorf(codes.Canceled, "recv data: %v", err)
		}
		b := in.GetData()
		if len(b) == 0 {
			continue
		}
		n, werr := tmp.Write(b)
		if werr != nil {
			return status.Errorf(codes.Internal, "write temp: %v", werr)
		}
		written += int64(n)
	}

	// 4) 校验 size（强烈建议校验：防止 part 丢字节/重传截断）
	if meta.PartSize > 0 && written != meta.PartSize {
		return status.Errorf(codes.InvalidArgument, "part_size mismatch: expect=%d got=%d", meta.PartSize, written)
	}

	// 5) 上传该 part 到 MinIO（PutObjectPart）
	if _, err := tmp.Seek(0, 0); err != nil {
		return status.Errorf(codes.Internal, "seek temp: %v", err)
	}

	etag, err := internal.MinIOClient.MinIOMultipartPutPart(
		ctx,
		objectKey,
		ossUploadID,
		int(meta.PartNumber),
		tmp,
		written,
		"", // md5Base64：你若需要可在网关算好后传进来
	)
	if err != nil {
		return status.Errorf(codes.Internal, "minio put part: %v", err)
	}

	// 6) 把 etag 写入 Redis：mp:parts:<upload_id>[partNumber] = etag
	pk := partsKey(meta.UploadId)
	if err := internal.RedisClient.HSet(ctx, pk, strconv.Itoa(int(meta.PartNumber)), etag).Err(); err != nil {
		return status.Errorf(codes.Internal, "redis HSet part etag: %v", err)
	}
	_ = internal.RedisClient.HSet(ctx, mk, "status", "uploading").Err()

	// 7) close response
	return stream.SendAndClose(&UploadPartResp{
		UploadId:   meta.UploadId,
		PartNumber: meta.PartNumber,
		PartSize:   written,
		Etag:       etag,
	})
}

// -------------------- CompleteMultipart（关键改动：改为 MinIO Complete，不做本地合并） --------------------
func (s FileServer) CompleteMultipart(ctx context.Context, req *CompleteReq) (*CompleteResp, error) {
	if req == nil || req.UploadId == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	mk := metaKey(req.UploadId)

	// 1) 取 meta：object_key / oss_upload_id / total_parts / content_type
	vals, err := internal.RedisClient.HMGet(ctx, mk, "object_key", "oss_upload_id", "total_parts", "content_type", "status").Result()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	objectKey, _ := vals[0].(string)
	ossUploadID, _ := vals[1].(string)
	totalPartsStr := fmt.Sprint(vals[2])
	contentType, _ := vals[3].(string)
	statusStr, _ := vals[4].(string)

	if objectKey == "" || ossUploadID == "" {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}

	// 已完成则幂等返回（建议：你可在 meta 存最终 etag/url）
	if statusStr == "completed" {
		return &CompleteResp{
			UploadId:  req.UploadId,
			ObjectKey: objectKey,
			Status:    "completed",
		}, nil
	}

	totalParts, _ := strconv.Atoi(totalPartsStr)
	if totalParts <= 0 {
		return nil, status.Error(codes.Internal, "invalid total_parts in meta")
	}

	// 2) 从 Redis 拉取所有 part 的 ETag
	pk := partsKey(req.UploadId)
	m, err := internal.RedisClient.HGetAll(ctx, pk).Result()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HGetAll parts: %v", err)
	}
	if len(m) != totalParts {
		return nil, status.Errorf(codes.FailedPrecondition, "parts not complete: got=%d expect=%d", len(m), totalParts)
	}

	// 3) 组装 []minio.CompletePart（必须包含 PartNumber + ETag）
	parts := make([]minio.CompletePart, 0, len(m))
	for k, etag := range m {
		pn, err := strconv.Atoi(k)
		if err != nil || pn <= 0 {
			return nil, status.Errorf(codes.Internal, "invalid part number key=%q", k)
		}
		parts = append(parts, minio.CompletePart{
			PartNumber: pn,
			ETag:       etag,
		})
	}

	// 4) 关键改动：调用 MinIO CompleteMultipartUpload（不再本地合并后 PutObject）
	info, err := internal.MinIOClient.MinIOMultipartComplete(
		ctx,
		objectKey,
		ossUploadID,
		parts,
		contentType,
		nil, // user meta：如果你 init 时写过，也可以这里再传（通常可不传）
	)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "minio complete multipart: %v", err)
	}

	// 5) 更新 meta 状态 + 保存最终 etag
	_ = internal.RedisClient.HSet(ctx, mk, map[string]any{
		"status": "completed",
		"etag":   info.ETag,
	}).Err()

	// 6) 返回
	return &CompleteResp{
		UploadId:  req.UploadId,
		ObjectKey: objectKey,
		Etag:      info.ETag,
		Status:    "completed",
	}, nil
}

// AbortMultipart：
// 1) 从 Redis 取出 object_key + minio_upload_id
// 2) 调 MinIO AbortMultipartUpload 回收未完成分片
// 3) 删除 Redis 中 meta/parts 状态（幂等：不存在也当成功）
func (s FileServer) AbortMultipart(ctx context.Context, req *AbortReq) (*AbortResp, error) {
	if req == nil || req.UploadId == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	mk := metaKey(req.UploadId)
	pk := partsKey(req.UploadId)

	// 读取 meta（字段名尽量兼容：minio_upload_id / oss_upload_id 二选一）
	vals, err := internal.RedisClient.HMGet(ctx, mk, "object_key", "minio_upload_id", "oss_upload_id", "status").Result()
	if err != nil && err != redis.Nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}

	// meta 不存在：直接当成功（幂等 abort）
	if len(vals) == 0 || (vals[0] == nil && vals[1] == nil && vals[2] == nil) {
		_ = internal.RedisClient.Del(ctx, mk, pk).Err()
		return &AbortResp{}, nil
	}

	objectKey, _ := vals[0].(string)

	minioUploadID, _ := vals[1].(string)
	if minioUploadID == "" {
		minioUploadID, _ = vals[2].(string) // oss_upload_id 兼容
	}

	statusStr, _ := vals[3].(string)

	// 如果已经 completed，你可以选择拒绝 abort；这里采取“已完成则只清理 redis，不回收 minio”
	if statusStr != "completed" && objectKey != "" && minioUploadID != "" {
		// 回收 MinIO multipart（避免残留未完成 upload 占用资源）
		if internal.MinIOClient != nil { // internal.MinIOClient 是全局变量 :contentReference[oaicite:1]{index=1}
			_ = internal.MinIOClient.MinIOMultipartAbort(ctx, objectKey, minioUploadID)
			// Abort 失败一般不影响继续清理 Redis（可按你风格改为强校验）
		}
	}

	// 删除 Redis 状态（meta + parts）
	_ = internal.RedisClient.Del(ctx, mk, pk).Err()

	return &AbortResp{}, nil
}

// Status：
// 1) 读取 total_parts
// 2) 读取已上传 part 列表（从 parts hash 的 field 里拿到 part_number）
// 3) 返回 uploaded_parts（升序）
func (s FileServer) Status(ctx context.Context, req *StatusReq) (*StatusResp, error) {
	if req == nil || req.UploadId == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	mk := metaKey(req.UploadId)
	pk := partsKey(req.UploadId)

	// total_parts
	totalStr, err := internal.RedisClient.HGet(ctx, mk, "total_parts").Result()
	if err == redis.Nil {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HGet total_parts: %v", err)
	}
	totalParts64, err := strconv.ParseInt(totalStr, 10, 32)
	if err != nil || totalParts64 < 0 {
		return nil, status.Errorf(codes.Internal, "invalid total_parts=%q", totalStr)
	}

	// 已上传 parts：hash 的 field 是 partNumber，value 是 etag
	keys, err := internal.RedisClient.HKeys(ctx, pk).Result()
	if err != nil && err != redis.Nil {
		return nil, status.Errorf(codes.Internal, "redis HKeys parts: %v", err)
	}

	uploaded := make([]int32, 0, len(keys))
	for _, k := range keys {
		pn, err := strconv.ParseInt(k, 10, 32)
		if err != nil || pn <= 0 {
			// 防御：跳过非法 field
			continue
		}
		uploaded = append(uploaded, int32(pn))
	}

	sort.Slice(uploaded, func(i, j int) bool { return uploaded[i] < uploaded[j] })

	return &StatusResp{
		TotalParts:    int32(totalParts64),
		UploadedParts: uploaded,
	}, nil
}

func (f FileServer) mustEmbedUnimplementedFileServiceServer() {

	panic("implement me")
}
