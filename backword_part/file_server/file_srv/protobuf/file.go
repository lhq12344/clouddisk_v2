package filepb

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"go_test/backword_part/model"
	"go_test/internal"
	"io"
	"mime"
	"net/url"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/go-redis/redis/v8"
	"github.com/goccy/go-json"
	"github.com/google/uuid"
	"github.com/minio/minio-go/v7"
	"go.uber.org/zap"
	codes "google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	status "google.golang.org/grpc/status"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

const (
	metaKeyPrefix          = "mp:meta:"
	defaultTTL             = 24 * time.Hour
	multipartPresignExpiry = 15 * time.Minute
)

var errObjectMissing = errors.New("file object missing in storage")

type FileServer struct {
	UnimplementedFileServiceServer
}

type uploadObjectResult struct {
	info minio.UploadInfo
	err  error
}

func genUploadID() string {
	var b [16]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

func metaKey(uploadID string) string { return metaKeyPrefix + uploadID }

func buildObjectKey(fileHash string) string {
	return fmt.Sprintf("files/%s", strings.TrimSpace(fileHash))
}

func buildAttachmentContentDisposition(filename string) string {
	name := strings.ReplaceAll(filename, `"`, "")
	return fmt.Sprintf(`attachment; filename="%s"; filename*=UTF-8''%s`,
		name, url.QueryEscape(name))
}

func buildInlineContentDisposition(filename string) string {
	name := strings.TrimSpace(filename)
	name = strings.ReplaceAll(name, "\r", "")
	name = strings.ReplaceAll(name, "\n", "")
	name = strings.ReplaceAll(name, `"`, "")
	return fmt.Sprintf(`inline; filename="%s"; filename*=UTF-8''%s`, name, url.QueryEscape(name))
}

func resolveDownloadFilename(filename string, file *model.File) string {
	name := strings.TrimSpace(filename)
	if name != "" {
		return name
	}
	if file == nil {
		return "download.bin"
	}
	if ext := filepath.Ext(strings.TrimSpace(file.ObjectKey)); ext != "" {
		return "download" + ext
	}
	return "download.bin"
}

func buildPresignedGetURL(objectKey, filename, contentDisposition, contentType string) (string, error) {
	reqParams := make(url.Values)
	if contentDisposition != "" {
		reqParams.Set("response-content-disposition", contentDisposition)
	}
	if contentType != "" {
		reqParams.Set("response-content-type", contentType)
	}
	return internal.MinIOClient.MinIOPresignGetURLWithParams(objectKey, 10*time.Minute, reqParams)
}

func detectMimeByName(filename, fallback string) string {
	ext := strings.ToLower(filepath.Ext(strings.TrimSpace(filename)))
	if ext != "" {
		if mt := mime.TypeByExtension(ext); mt != "" {
			return mt
		}
	}
	if fallback != "" {
		return fallback
	}
	return "application/octet-stream"
}

func reuseMessage(status string, scanQueued bool) string {
	switch status {
	case model.FileSuccess:
		return "existing clean object reused"
	case model.FilePendingScan:
		if scanQueued {
			return "existing object reused, virus scan queued"
		}
		return "existing object pending virus scan"
	case model.FileInfected:
		return "file blocked by virus scan"
	default:
		if scanQueued {
			return "existing object reused, virus scan queued"
		}
		return "existing object reused"
	}
}

func ensureAccount(ctx context.Context, userID string) (*model.Account, error) {
	var account model.Account
	if err := internal.DB.WithContext(ctx).First(&account, userID).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return nil, fmt.Errorf("account not found")
		}
		return nil, err
	}
	return &account, nil
}

func incomingUserID(ctx context.Context) string {
	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		return ""
	}
	values := md.Get("x-user-id")
	if len(values) == 0 {
		return ""
	}
	return strings.TrimSpace(values[0])
}

func ensureMultipartOwner(ctx context.Context, ownerUserID string) error {
	currentUserID := incomingUserID(ctx)
	if currentUserID == "" {
		return status.Error(codes.Unauthenticated, "missing user identity")
	}
	if strings.TrimSpace(ownerUserID) != currentUserID {
		return status.Error(codes.PermissionDenied, "upload session does not belong to current user")
	}
	return nil
}

func ensureUserFile(tx *gorm.DB, accountID, fileID uint, fileName string) error {
	var existing model.UserFile
	err := tx.Unscoped().
		Where("account_id = ? AND file_id = ? AND name = ?", accountID, fileID, fileName).
		Take(&existing).Error
	if err == nil {
		if !existing.DeletedAt.Valid {
			return nil
		}
		return tx.Unscoped().
			Model(&model.UserFile{}).
			Where("id = ?", existing.ID).
			Updates(map[string]any{
				"deleted_at": nil,
				"updated_at": time.Now(),
			}).Error
	}
	if err != nil && !errors.Is(err, gorm.ErrRecordNotFound) {
		return err
	}

	uf := model.UserFile{
		AccountID: accountID,
		FileID:    fileID,
		Name:      fileName,
	}
	if err := tx.Clauses(clause.OnConflict{
		Columns:   []clause.Column{{Name: "account_id"}, {Name: "file_id"}, {Name: "name"}},
		DoNothing: true,
	}).Create(&uf).Error; err != nil {
		return err
	}

	return tx.Unscoped().
		Model(&model.UserFile{}).
		Where("account_id = ? AND file_id = ? AND name = ? AND deleted_at IS NOT NULL", accountID, fileID, fileName).
		Updates(map[string]any{
			"deleted_at": nil,
			"updated_at": time.Now(),
		}).Error
}

func duplicateKey(err error) bool {
	if err == nil {
		return false
	}
	msg := err.Error()
	return strings.Contains(msg, "Duplicate entry") || strings.Contains(msg, "Error 1062")
}

func marshalOutboxHeaders(ctx context.Context) string {
	headers := map[string]string{}
	if rid := internal.RequestIDFromContext(ctx); rid != "" {
		headers["x-request-id"] = rid
	}
	b, _ := json.Marshal(headers)
	return string(b)
}

func enqueueScanRequested(ctx context.Context, tx *gorm.DB, file *model.File, userID string) error {
	txID := uuid.NewString()
	eventID := txID + ":SCAN_REQUESTED"
	payload := model.FileEventPayload{
		TxID:        txID,
		EventID:     eventID,
		FileID:      file.ID,
		UserID:      userID,
		Sha1:        file.Sha1,
		Size:        file.Size,
		OssKey:      file.ObjectKey,
		ContentType: file.ContentType,
		EventType:   model.FileScanRequested,
	}
	body, _ := json.Marshal(payload)

	return tx.Create(&model.Outbox{
		EventID:     eventID,
		TxID:        txID,
		EventType:   model.FileScanRequested,
		Topic:       model.FileUploadEventTopic,
		Key:         file.Sha1,
		Payload:     string(body),
		Headers:     marshalOutboxHeaders(ctx),
		Status:      model.OutboxNew,
		RetryCount:  0,
		NextRetryAt: time.Now(),
	}).Error
}

func upsertFileAndRelation(
	ctx context.Context,
	account *model.Account,
	userID, fileName, fileHash, contentType, objectKey string,
	fileSize int64,
	requeueScan bool,
) (*model.File, bool, error) {
	var (
		file          model.File
		shouldEnqueue bool
	)

	err := internal.DB.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		err := tx.Unscoped().Where("sha1 = ?", fileHash).Take(&file).Error
		if err != nil && !errors.Is(err, gorm.ErrRecordNotFound) {
			return err
		}

		if errors.Is(err, gorm.ErrRecordNotFound) {
			file = model.File{
				Sha1:        fileHash,
				Size:        fileSize,
				Status:      model.FilePendingScan,
				ObjectKey:   objectKey,
				ContentType: contentType,
			}
			if err := tx.Create(&file).Error; err != nil {
				if !duplicateKey(err) {
					return err
				}
				if err := tx.Unscoped().Where("sha1 = ?", fileHash).Take(&file).Error; err != nil {
					return err
				}
				shouldEnqueue = true
			} else {
				shouldEnqueue = true
			}
		}

		if file.DeletedAt.Valid {
			restoreUpdates := map[string]any{
				"deleted_at": nil,
				"updated_at": time.Now(),
			}
			if objectKey != "" {
				restoreUpdates["object_key"] = objectKey
				file.ObjectKey = objectKey
			}
			if contentType != "" {
				restoreUpdates["content_type"] = contentType
				file.ContentType = contentType
			}
			if fileSize > 0 {
				restoreUpdates["size"] = fileSize
				file.Size = fileSize
			}
			if err := tx.Unscoped().Model(&model.File{}).Where("id = ?", file.ID).Updates(restoreUpdates).Error; err != nil {
				return err
			}
			file.DeletedAt = gorm.DeletedAt{}
			shouldEnqueue = true
		}

		metadataUpdates := map[string]any{}
		if objectKey != "" && file.ObjectKey == "" {
			metadataUpdates["object_key"] = objectKey
			file.ObjectKey = objectKey
		}
		if contentType != "" && file.ContentType == "" {
			metadataUpdates["content_type"] = contentType
			file.ContentType = contentType
		}
		if fileSize > 0 && file.Size == 0 {
			metadataUpdates["size"] = fileSize
			file.Size = fileSize
		}
		if len(metadataUpdates) > 0 {
			if err := tx.Model(&model.File{}).Where("id = ?", file.ID).Updates(metadataUpdates).Error; err != nil {
				return err
			}
		}

		switch file.Status {
		case model.FileInfected:
			return fmt.Errorf("file blocked by virus scan")
		case model.FilePendingScan, model.FileSuccess:
			// 已有对象已在扫描中或已通过扫描，只补 user_files 关系，不重复投递扫描事件。
		case model.FileScanFailed:
			updates := map[string]any{
				"status":      model.FilePendingScan,
				"scan_detail": "",
				"scanned_at":  nil,
			}
			if objectKey != "" {
				updates["object_key"] = objectKey
			}
			if contentType != "" {
				updates["content_type"] = contentType
			}
			if fileSize > 0 {
				updates["size"] = fileSize
			}
			if err := tx.Model(&model.File{}).Where("id = ?", file.ID).Updates(updates).Error; err != nil {
				return err
			}
			file.Status = model.FilePendingScan
			file.ScanDetail = ""
			file.ScannedAt = nil
			if objectKey != "" {
				file.ObjectKey = objectKey
			}
			if contentType != "" {
				file.ContentType = contentType
			}
			if fileSize > 0 {
				file.Size = fileSize
			}
			shouldEnqueue = true
		case "":
			// 兼容历史脏数据：旧记录 status 为空时回填为 pending_scan 并补一条扫描事件。
			if err := tx.Model(&model.File{}).Where("id = ?", file.ID).Updates(map[string]any{
				"status":       model.FilePendingScan,
				"object_key":   objectKey,
				"content_type": contentType,
				"size":         fileSize,
			}).Error; err != nil {
				return err
			}
			file.Status = model.FilePendingScan
			file.ObjectKey = objectKey
			file.ContentType = contentType
			file.Size = fileSize
			shouldEnqueue = true
		}

		if requeueScan && file.Status != model.FilePendingScan {
			if err := tx.Model(&model.File{}).Where("id = ?", file.ID).Updates(map[string]any{
				"status":      model.FilePendingScan,
				"scan_detail": "",
				"scanned_at":  nil,
			}).Error; err != nil {
				return err
			}
			file.Status = model.FilePendingScan
			file.ScanDetail = ""
			file.ScannedAt = nil
			shouldEnqueue = true
		}

		if err := ensureUserFile(tx, account.ID, file.ID, fileName); err != nil {
			return err
		}

		if shouldEnqueue {
			if err := enqueueScanRequested(ctx, tx, &file, userID); err != nil {
				return err
			}
		}
		return nil
	})
	if err != nil {
		return nil, false, err
	}
	return &file, shouldEnqueue, nil
}

func findFileByHash(ctx context.Context, fileHash string) (*model.File, error) {
	var file model.File
	err := internal.DB.WithContext(ctx).Where("sha1 = ?", fileHash).Take(&file).Error
	if errors.Is(err, gorm.ErrRecordNotFound) {
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	return &file, nil
}

func drainUploadFileStream(stream FileService_UploadFileServer) error {
	for {
		msg, err := stream.Recv()
		if err == io.EOF {
			return nil
		}
		if err != nil {
			return err
		}
		if len(msg.GetData()) == 0 {
			continue
		}
	}
}

func resolveStoredObjectKey(file *model.File) string {
	if file == nil {
		return ""
	}
	if key := strings.TrimSpace(file.ObjectKey); key != "" {
		return key
	}
	if hash := strings.TrimSpace(file.Sha1); hash != "" {
		return buildObjectKey(hash)
	}
	return ""
}

func fileUnavailableMessage(file *model.File) string {
	if file == nil {
		return "file is not available for download"
	}
	if detail := strings.TrimSpace(file.ScanDetail); detail != "" &&
		(file.Status == model.FileScanFailed || file.Status == model.FileInfected) {
		return detail
	}
	switch file.Status {
	case model.FilePendingScan:
		return "file is pending virus scan"
	case model.FileInfected:
		return "file blocked by virus scan"
	case model.FileScanFailed:
		if detail := strings.TrimSpace(file.ScanDetail); detail != "" {
			return detail
		}
		return "file scan failed"
	default:
		return "file is not available for download"
	}
}

func markFileObjectMissing(ctx context.Context, file *model.File, objectKey string) {
	if file == nil || file.ID == 0 {
		return
	}
	detail := "file object missing in storage"
	updates := map[string]any{
		"status":      model.FileScanFailed,
		"scan_detail": detail,
		"updated_at":  time.Now(),
	}
	if strings.TrimSpace(file.ObjectKey) == "" && objectKey != "" {
		updates["object_key"] = objectKey
	}
	if err := internal.DB.WithContext(ctx).
		Model(&model.File{}).
		Where("id = ?", file.ID).
		Updates(updates).Error; err != nil {
		internal.Logger.Warn("[file]mark missing object failed",
			zap.Uint("file_id", file.ID),
			zap.String("object_key", objectKey),
			zap.Error(err))
	}
	file.Status = model.FileScanFailed
	file.ScanDetail = detail
	if strings.TrimSpace(file.ObjectKey) == "" {
		file.ObjectKey = objectKey
	}
}

func ensureStoredObjectAvailable(ctx context.Context, file *model.File) (string, error) {
	if file == nil || file.ID == 0 {
		return "", gorm.ErrRecordNotFound
	}
	objectKey := resolveStoredObjectKey(file)
	if objectKey == "" {
		markFileObjectMissing(ctx, file, objectKey)
		return "", errObjectMissing
	}
	if internal.MinIOClient == nil || internal.MinIOClient.Client == nil {
		return objectKey, fmt.Errorf("object storage unavailable")
	}

	exists, err := internal.MinIOClient.MinIOObjectExists(objectKey)
	if err != nil {
		return objectKey, err
	}
	if !exists {
		markFileObjectMissing(ctx, file, objectKey)
		return objectKey, errObjectMissing
	}
	if strings.TrimSpace(file.ObjectKey) == "" {
		if err := internal.DB.WithContext(ctx).
			Model(&model.File{}).
			Where("id = ?", file.ID).
			Updates(map[string]any{
				"object_key": objectKey,
				"updated_at": time.Now(),
			}).Error; err != nil {
			internal.Logger.Warn("[file]persist object key failed",
				zap.Uint("file_id", file.ID),
				zap.String("object_key", objectKey),
				zap.Error(err))
		} else {
			file.ObjectKey = objectKey
		}
	}
	return objectKey, nil
}

func findOwnedUserFile(ctx context.Context, accountID uint, filename, fileHash string) (*model.UserFile, error) {
	var uf model.UserFile

	q := internal.DB.WithContext(ctx).
		Preload("File").
		Where("user_files.account_id = ?", accountID)
	if filename = strings.TrimSpace(filename); filename != "" {
		q = q.Where("user_files.name = ?", filename)
	}
	if fileHash = strings.TrimSpace(fileHash); fileHash != "" {
		q = q.Joins("JOIN files ON files.id = user_files.file_id AND files.deleted_at IS NULL").
			Where("files.sha1 = ?", fileHash)
	}
	if err := q.Order("user_files.id desc").Take(&uf).Error; err != nil {
		return nil, err
	}
	if uf.File.ID == 0 {
		return nil, gorm.ErrRecordNotFound
	}
	return &uf, nil
}

func fileUnavailableResp(file *model.File) (*RespResolveFileHash, error) {
	return &RespResolveFileHash{
		Code:    409,
		Message: fileUnavailableMessage(file),
	}, nil
}

func listUploadedParts(ctx context.Context, objectKey, ossUploadID string) ([]minio.ObjectPart, error) {
	parts, err := internal.MinIOClient.MinIOMultipartListParts(ctx, objectKey, ossUploadID)
	if err != nil {
		return nil, err
	}
	sort.Slice(parts, func(i, j int) bool { return parts[i].PartNumber < parts[j].PartNumber })
	return parts, nil
}

func (f FileServer) Filedowm(ctx context.Context, req *ReqFileDown) (*Resp, error) {
	l := internal.LoggerWithRID(ctx, internal.Logger)
	account, err := ensureAccount(ctx, req.Userid)
	if err != nil {
		l.Error("[Download]account not found", zap.Error(err))
		return &Resp{Code: 404, Message: err.Error()}, nil
	}

	uf, err := findOwnedUserFile(ctx, account.ID, req.Filename, req.Filehash)
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return &Resp{Code: 404, Message: "file not found"}, nil
		}
		return nil, err
	}
	file := &uf.File
	if file.Status != model.FileSuccess {
		return &Resp{Code: 409, Message: fileUnavailableMessage(file)}, nil
	}

	objectKey, err := ensureStoredObjectAvailable(ctx, file)
	if err != nil {
		if errors.Is(err, errObjectMissing) {
			return &Resp{Code: 409, Message: fileUnavailableMessage(file)}, nil
		}
		l.Error("[Download]check object", zap.Uint("file_id", file.ID), zap.Error(err))
		return &Resp{Code: 503, Message: "object storage unavailable"}, nil
	}

	filename := resolveDownloadFilename(req.Filename, file)
	contentType := detectMimeByName(filename, strings.TrimSpace(file.ContentType))
	u, err := buildPresignedGetURL(objectKey, filename, buildAttachmentContentDisposition(filename), contentType)
	if err != nil {
		l.Error("[Download]sign url", zap.Error(err))
		return nil, fmt.Errorf("[Download]generate oss signed url failed: %w", err)
	}
	return &Resp{Code: 0, Message: u}, nil
}

func (f FileServer) LoadFile(ctx context.Context, req *Reqloadfile) (*Resp, error) {
	return &Resp{Code: 410, Message: "deprecated, use streaming UploadFile"}, nil
}

func (s FileServer) UploadFile(stream FileService_UploadFileServer) error {
	ctx := stream.Context()
	l := internal.LoggerWithRID(ctx, internal.Logger)

	first, err := stream.Recv()
	if err != nil {
		return status.Errorf(codes.InvalidArgument, "recv meta: %v", err)
	}

	meta := first.GetMeta()
	if meta == nil {
		return status.Error(codes.InvalidArgument, "first message must be meta")
	}
	meta.FileHash = strings.TrimSpace(meta.FileHash)
	meta.FileName = strings.TrimSpace(meta.FileName)
	meta.UserId = strings.TrimSpace(meta.UserId)
	meta.ContentType = detectMimeByName(meta.FileName, meta.ContentType)
	if meta.UserId == "" || meta.FileName == "" || meta.FileHash == "" || meta.FileSize < 0 {
		return status.Error(codes.InvalidArgument, "missing user_id/file_name/file_hash or invalid size")
	}

	account, err := ensureAccount(ctx, meta.UserId)
	if err != nil {
		return status.Error(codes.NotFound, err.Error())
	}

	existing, err := findFileByHash(ctx, meta.FileHash)
	if err != nil {
		return status.Errorf(codes.Internal, "query file by hash: %v", err)
	}
	if existing != nil {
		if err := drainUploadFileStream(stream); err != nil {
			return status.Errorf(codes.Canceled, "drain duplicate upload stream: %v", err)
		}
		file, scanQueued, err := upsertFileAndRelation(ctx, account, meta.UserId, meta.FileName, meta.FileHash, meta.ContentType, existing.ObjectKey, meta.FileSize, existing.Status == model.FileScanFailed)
		if err != nil {
			if strings.Contains(err.Error(), "blocked by virus scan") {
				return stream.SendAndClose(&UploadFileResp{
					FileHash:  meta.FileHash,
					ObjectKey: existing.ObjectKey,
					Status:    model.FileInfected,
					Message:   err.Error(),
				})
			}
			return status.Errorf(codes.Internal, "attach existing file: %v", err)
		}
		return stream.SendAndClose(&UploadFileResp{
			FileHash:  file.Sha1,
			ObjectKey: file.ObjectKey,
			Status:    file.Status,
			Message:   reuseMessage(file.Status, scanQueued),
		})
	}

	objectKey := buildObjectKey(meta.FileHash)
	pipeReader, pipeWriter := io.Pipe()
	uploadResultCh := make(chan uploadObjectResult, 1)
	go func() {
		info, uploadErr := internal.MinIOClient.MinIOUploadStream(
			ctx,
			pipeReader,
			meta.FileSize,
			objectKey,
			meta.ContentType,
			map[string]string{
				"x-file-name": meta.FileName,
				"x-file-hash": meta.FileHash,
				"x-user-id":   meta.UserId,
			},
		)
		uploadResultCh <- uploadObjectResult{info: info, err: uploadErr}
	}()

	var written int64
	for {
		msg, recvErr := stream.Recv()
		if recvErr == io.EOF {
			break
		}
		if recvErr != nil {
			_ = pipeWriter.CloseWithError(recvErr)
			<-uploadResultCh
			return status.Errorf(codes.Canceled, "recv data: %v", recvErr)
		}
		chunk := msg.GetData()
		if len(chunk) == 0 {
			continue
		}
		n, writeErr := pipeWriter.Write(chunk)
		if writeErr != nil {
			_ = pipeWriter.CloseWithError(writeErr)
			<-uploadResultCh
			return status.Errorf(codes.Internal, "stream write: %v", writeErr)
		}
		written += int64(n)
	}

	if written != meta.FileSize {
		sizeErr := fmt.Errorf("file_size mismatch: expect=%d got=%d", meta.FileSize, written)
		_ = pipeWriter.CloseWithError(sizeErr)
		<-uploadResultCh
		return status.Error(codes.InvalidArgument, sizeErr.Error())
	}

	if err := pipeWriter.Close(); err != nil {
		<-uploadResultCh
		return status.Errorf(codes.Internal, "close upload pipe: %v", err)
	}

	uploadResult := <-uploadResultCh
	if uploadResult.err != nil {
		return status.Errorf(codes.Internal, "upload to object storage: %v", uploadResult.err)
	}

	file, _, err := upsertFileAndRelation(ctx, account, meta.UserId, meta.FileName, meta.FileHash, meta.ContentType, objectKey, meta.FileSize, false)
	if err != nil {
		_ = internal.MinIOClient.MinIODeleteObject(objectKey)
		if strings.Contains(err.Error(), "blocked by virus scan") {
			return stream.SendAndClose(&UploadFileResp{
				FileHash:  meta.FileHash,
				ObjectKey: objectKey,
				Status:    model.FileInfected,
				Message:   err.Error(),
			})
		}
		l.Error("[UploadFile]finalize uploaded file failed", zap.Error(err))
		return status.Errorf(codes.Internal, "finalize uploaded file: %v", err)
	}

	return stream.SendAndClose(&UploadFileResp{
		FileHash:  file.Sha1,
		ObjectKey: file.ObjectKey,
		Status:    file.Status,
		Message:   "uploaded",
	})
}

func (f FileServer) Showfile(ctx context.Context, req *Reqshowfile) (*Resp, error) {
	account, err := ensureAccount(ctx, req.Userid)
	if err != nil {
		return &Resp{Code: 404, Message: err.Error()}, nil
	}

	uf, err := findOwnedUserFile(ctx, account.ID, req.Filename, req.Filehash)
	if err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return &Resp{Code: 404, Message: "file not found"}, nil
		}
		return nil, err
	}
	file := &uf.File
	if file.Status != model.FileSuccess {
		return &Resp{Code: 409, Message: fileUnavailableMessage(file)}, nil
	}

	objectKey, err := ensureStoredObjectAvailable(ctx, file)
	if err != nil {
		if errors.Is(err, errObjectMissing) {
			return &Resp{Code: 409, Message: fileUnavailableMessage(file)}, nil
		}
		return &Resp{Code: 503, Message: "object storage unavailable"}, nil
	}

	filename := resolveDownloadFilename(req.Filename, file)
	contentType := detectMimeByName(filename, strings.TrimSpace(file.ContentType))
	u, err := buildPresignedGetURL(objectKey, filename, buildInlineContentDisposition(filename), contentType)
	if err != nil {
		return nil, fmt.Errorf("[Showfile]generate oss signed url failed: %w", err)
	}
	return &Resp{Code: 0, Message: u}, nil
}

func (f FileServer) DeleteFile(ctx context.Context, req *ReqDeleteFile) (*Resp, error) {
	userID := strings.TrimSpace(req.Userid)
	filename := strings.TrimSpace(req.Filename)
	filehash := strings.TrimSpace(req.Filehash)
	if userID == "" || filename == "" {
		return &Resp{Code: 400, Message: "missing userid/filename"}, nil
	}

	account, err := ensureAccount(ctx, userID)
	if err != nil {
		return &Resp{Code: 404, Message: err.Error()}, nil
	}

	var uf model.UserFile
	tx := internal.DB.WithContext(ctx)
	q := tx.Preload("File").Where("account_id = ? AND name = ?", account.ID, filename)
	if filehash != "" {
		q = q.Joins("JOIN files ON files.id = user_files.file_id AND files.sha1 = ?", filehash)
	}
	if err := q.Take(&uf).Error; err != nil {
		if errors.Is(err, gorm.ErrRecordNotFound) {
			return &Resp{Code: 404, Message: "file not found"}, nil
		}
		return nil, err
	}
	if uf.File.Status == model.FilePendingScan {
		return &Resp{Code: 409, Message: "file is pending virus scan"}, nil
	}

	fileID := uf.FileID
	objectKey := uf.File.ObjectKey
	if objectKey == "" && uf.File.Sha1 != "" {
		objectKey = buildObjectKey(uf.File.Sha1)
	}

	var remaining int64
	if err := tx.Transaction(func(tx *gorm.DB) error {
		if err := tx.Where("account_id = ? AND file_id = ? AND name = ?", account.ID, fileID, uf.Name).
			Delete(&model.UserFile{}).Error; err != nil {
			return err
		}
		if err := tx.Model(&model.UserFile{}).Where("file_id = ?", fileID).Count(&remaining).Error; err != nil {
			return err
		}
		if remaining == 0 {
			if err := tx.Where("id = ?", fileID).Delete(&model.File{}).Error; err != nil {
				return err
			}
		}
		return nil
	}); err != nil {
		return nil, err
	}

	if remaining == 0 && objectKey != "" && internal.MinIOClient != nil && internal.MinIOClient.Client != nil {
		ctx2, cancel := context.WithTimeout(ctx, 10*time.Second)
		defer cancel()
		if err := internal.MinIOClient.Client.RemoveObject(ctx2, internal.MinIOClient.Bucket, objectKey, minio.RemoveObjectOptions{}); err != nil {
			internal.Logger.Warn("[DeleteFile]remove object failed", zap.String("objectKey", objectKey), zap.Error(err))
		}
	}

	return &Resp{Code: 0, Message: "deleted"}, nil
}

func (f FileServer) Filequeryinfo(ctx context.Context, req *ReqFileQuery) (*RespFileQuery, error) {
	account, err := ensureAccount(ctx, req.Userid)
	if err != nil {
		return &RespFileQuery{Code: 404, Message: err.Error()}, nil
	}

	var ufs []model.UserFile
	if err := internal.DB.WithContext(ctx).
		Where("account_id = ?", account.ID).
		Preload("File").
		Order("id desc").
		Find(&ufs).Error; err != nil {
		return &RespFileQuery{Code: 500, Message: err.Error()}, nil
	}

	resp := &RespFileQuery{
		Code:    0,
		Message: "ok",
		Files:   make([]*FileInfo, 0, len(ufs)),
	}
	for _, uf := range ufs {
		if uf.File.ID == 0 {
			internal.Logger.Warn("[Filequeryinfo]skip dangling relation",
				zap.Uint("user_file_id", uf.ID),
				zap.Uint("account_id", account.ID),
				zap.String("filename", uf.Name))
			continue
		}
		if uf.File.Status == model.FileSuccess {
			if _, err := ensureStoredObjectAvailable(ctx, &uf.File); err != nil && !errors.Is(err, errObjectMissing) {
				internal.Logger.Warn("[Filequeryinfo]stat object failed",
					zap.Uint("file_id", uf.File.ID),
					zap.String("filename", uf.Name),
					zap.Error(err))
			}
		}
		resp.Files = append(resp.Files, &FileInfo{
			FileHash:    uf.File.Sha1,
			FileName:    uf.Name,
			FileSizes:   uf.File.Size,
			UploadAt:    uf.File.CreatedAt.String(),
			LastUpdated: uf.File.UpdatedAt.String(),
			Status:      uf.File.Status,
			ScanDetail:  uf.File.ScanDetail,
			ContentType: uf.File.ContentType,
		})
	}
	return resp, nil
}

func (s FileServer) ResolveFileHash(ctx context.Context, req *ReqResolveFileHash) (*RespResolveFileHash, error) {
	filename := strings.TrimSpace(req.GetFilename())
	if filename == "" {
		return &RespResolveFileHash{Code: 400, Message: "filename is required"}, nil
	}

	uid64, err := strconv.ParseUint(strings.TrimSpace(req.GetUserid()), 10, 64)
	if err != nil || uid64 == 0 {
		return &RespResolveFileHash{Code: 400, Message: "invalid userid"}, nil
	}

	uf, err := findOwnedUserFile(ctx, uint(uid64), filename, "")
	if errors.Is(err, gorm.ErrRecordNotFound) {
		return &RespResolveFileHash{Code: 404, Message: "file not found"}, nil
	}
	if err != nil {
		return &RespResolveFileHash{Code: 500, Message: "db error: " + err.Error()}, nil
	}
	if uf.File.Status != model.FileSuccess {
		return fileUnavailableResp(&uf.File)
	}
	if _, err := ensureStoredObjectAvailable(ctx, &uf.File); err != nil {
		if errors.Is(err, errObjectMissing) {
			return fileUnavailableResp(&uf.File)
		}
		return &RespResolveFileHash{Code: 503, Message: "object storage unavailable"}, nil
	}

	return &RespResolveFileHash{
		Code:     200,
		Message:  "ok",
		FileHash: uf.File.Sha1,
		FileSize: uf.File.Size,
	}, nil
}

func (s FileServer) InitMultipart(ctx context.Context, req *InitReq) (*InitResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil req")
	}
	req.UserId = strings.TrimSpace(req.UserId)
	req.FileName = strings.TrimSpace(req.FileName)
	req.FileHash = strings.TrimSpace(req.FileHash)
	req.ContentType = detectMimeByName(req.FileName, req.ContentType)
	if req.UserId == "" || req.FileName == "" || req.FileHash == "" {
		return nil, status.Error(codes.InvalidArgument, "missing user_id/file_name/file_hash")
	}
	if req.FileSize <= 0 {
		return nil, status.Error(codes.InvalidArgument, "file_size must be > 0")
	}

	account, err := ensureAccount(ctx, req.UserId)
	if err != nil {
		return nil, status.Error(codes.NotFound, err.Error())
	}

	existing, err := findFileByHash(ctx, req.FileHash)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "query file by hash: %v", err)
	}
	if existing != nil {
		file, scanQueued, err := upsertFileAndRelation(ctx, account, req.UserId, req.FileName, req.FileHash, req.ContentType, existing.ObjectKey, req.FileSize, existing.Status == model.FileScanFailed)
		if err != nil {
			return nil, status.Error(codes.FailedPrecondition, err.Error())
		}
		return &InitResp{
			ObjectKey: file.ObjectKey,
			Status:    file.Status,
			Message:   reuseMessage(file.Status, scanQueued),
		}, nil
	}

	totalParts, partSize, _, err := minio.OptimalPartInfo(req.FileSize, 0)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "OptimalPartInfo: %v", err)
	}
	if totalParts <= 0 || partSize <= 0 {
		totalParts = 1
		partSize = req.FileSize
	}
	if totalParts <= 0 || partSize <= 0 {
		return nil, status.Error(codes.Internal, "invalid total parts")
	}

	uploadID := genUploadID()
	objectKey := buildObjectKey(req.FileHash)
	ossUploadID, err := internal.MinIOClient.MinIOMultipartInit(ctx, objectKey, req.ContentType, map[string]string{
		"x-file-name": req.FileName,
		"x-file-hash": req.FileHash,
		"x-user-id":   req.UserId,
	})
	if err != nil {
		return nil, status.Errorf(codes.Internal, "minio init multipart: %v", err)
	}

	if _, err := internal.RedisClient.TxPipelined(ctx, func(pipe redis.Pipeliner) error {
		pipe.HSet(ctx, metaKey(uploadID), map[string]any{
			"user_id":       req.UserId,
			"file_name":     req.FileName,
			"file_size":     req.FileSize,
			"file_hash":     req.FileHash,
			"content_type":  req.ContentType,
			"object_key":    objectKey,
			"part_size":     partSize,
			"total_parts":   totalParts,
			"oss_upload_id": ossUploadID,
			"status":        "init",
		})
		pipe.Expire(ctx, metaKey(uploadID), defaultTTL)
		return nil
	}); err != nil {
		_ = internal.MinIOClient.MinIOMultipartAbort(ctx, objectKey, ossUploadID)
		return nil, status.Errorf(codes.Internal, "redis save meta: %v", err)
	}

	return &InitResp{
		UploadId:   uploadID,
		ObjectKey:  objectKey,
		PartSize:   partSize,
		TotalParts: int32(totalParts),
		Status:     "init",
		Message:    "multipart initialized",
	}, nil
}

func (s FileServer) PresignParts(ctx context.Context, req *PresignPartsReq) (*PresignPartsResp, error) {
	if req == nil || strings.TrimSpace(req.UploadId) == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	vals, err := internal.RedisClient.HMGet(ctx, metaKey(req.UploadId), "user_id", "object_key", "oss_upload_id", "total_parts").Result()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	ownerUserID, _ := vals[0].(string)
	objectKey, _ := vals[1].(string)
	ossUploadID, _ := vals[2].(string)
	totalParts, _ := strconv.Atoi(fmt.Sprint(vals[3]))
	if ownerUserID == "" || objectKey == "" || ossUploadID == "" || totalParts <= 0 {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}
	if err := ensureMultipartOwner(ctx, ownerUserID); err != nil {
		return nil, err
	}

	resp := &PresignPartsResp{
		UploadId: req.UploadId,
		Parts:    make([]*PresignedPart, 0, len(req.PartNumbers)),
	}
	expiresAt := time.Now().Add(multipartPresignExpiry).Format(time.RFC3339)
	for _, partNumber := range req.PartNumbers {
		if partNumber <= 0 || int(partNumber) > totalParts {
			return nil, status.Errorf(codes.InvalidArgument, "invalid part_number=%d", partNumber)
		}
		u, err := internal.MinIOClient.MinIOPresignMultipartPutURL(ctx, objectKey, ossUploadID, int(partNumber), multipartPresignExpiry)
		if err != nil {
			return nil, status.Errorf(codes.Internal, "presign part %d: %v", partNumber, err)
		}
		resp.Parts = append(resp.Parts, &PresignedPart{
			PartNumber: partNumber,
			Url:        u,
			ExpiresAt:  expiresAt,
		})
	}
	sort.Slice(resp.Parts, func(i, j int) bool { return resp.Parts[i].PartNumber < resp.Parts[j].PartNumber })
	return resp, nil
}

func (s FileServer) UploadPart(stream FileService_UploadPartServer) error {
	return status.Error(codes.Unimplemented, "deprecated, use PresignParts + direct multipart upload")
}

func (s FileServer) CompleteMultipart(ctx context.Context, req *CompleteReq) (*CompleteResp, error) {
	if req == nil || strings.TrimSpace(req.UploadId) == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	vals, err := internal.RedisClient.HMGet(ctx, metaKey(req.UploadId),
		"user_id", "file_name", "file_size", "file_hash", "content_type", "object_key", "oss_upload_id", "total_parts", "status").Result()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	userID, _ := vals[0].(string)
	fileName, _ := vals[1].(string)
	fileSize, _ := strconv.ParseInt(fmt.Sprint(vals[2]), 10, 64)
	fileHash, _ := vals[3].(string)
	contentType, _ := vals[4].(string)
	objectKey, _ := vals[5].(string)
	ossUploadID, _ := vals[6].(string)
	totalParts, _ := strconv.Atoi(fmt.Sprint(vals[7]))
	statusStr, _ := vals[8].(string)
	if userID == "" || fileName == "" || fileHash == "" || objectKey == "" || ossUploadID == "" || totalParts <= 0 {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}
	if err := ensureMultipartOwner(ctx, userID); err != nil {
		return nil, err
	}

	if statusStr == "completed" {
		currentStatus := model.FilePendingScan
		if file, err := findFileByHash(ctx, fileHash); err == nil && file != nil && file.Status != "" {
			currentStatus = file.Status
		}
		return &CompleteResp{
			UploadId:  req.UploadId,
			ObjectKey: objectKey,
			Status:    currentStatus,
		}, nil
	}

	parts, err := listUploadedParts(ctx, objectKey, ossUploadID)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "list multipart parts: %v", err)
	}
	if len(parts) != totalParts {
		return nil, status.Errorf(codes.FailedPrecondition, "parts not complete: got=%d expect=%d", len(parts), totalParts)
	}

	completeParts := make([]minio.CompletePart, 0, len(parts))
	for _, part := range parts {
		completeParts = append(completeParts, minio.CompletePart{
			PartNumber: part.PartNumber,
			ETag:       part.ETag,
		})
	}

	info, err := internal.MinIOClient.MinIOMultipartComplete(ctx, objectKey, ossUploadID, completeParts, contentType, nil)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "minio complete multipart: %v", err)
	}

	account, err := ensureAccount(ctx, userID)
	if err != nil {
		return nil, status.Error(codes.NotFound, err.Error())
	}

	// requeueScan=false 是有意的：
	// 1. 新文件首次落库时会在 upsert 内部写 pending_scan + outbox。
	// 2. 复用 success/pending_scan 对象时不应重复投递扫描。
	// 3. 复用 scan_failed 对象时，upsert 会在 FileScanFailed 分支内重新排队扫描。
	file, _, err := upsertFileAndRelation(ctx, account, userID, fileName, fileHash, contentType, objectKey, fileSize, false)
	if err != nil {
		_ = internal.MinIOClient.MinIODeleteObject(objectKey)
		return nil, status.Errorf(codes.Internal, "finalize multipart upload: %v", err)
	}

	_ = internal.RedisClient.HSet(ctx, metaKey(req.UploadId), map[string]any{
		"status": "completed",
		"etag":   info.ETag,
	}).Err()

	return &CompleteResp{
		UploadId:  req.UploadId,
		ObjectKey: file.ObjectKey,
		Etag:      info.ETag,
		Status:    file.Status,
	}, nil
}

func (s FileServer) AbortMultipart(ctx context.Context, req *AbortReq) (*AbortResp, error) {
	if req == nil || strings.TrimSpace(req.UploadId) == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	vals, err := internal.RedisClient.HMGet(ctx, metaKey(req.UploadId), "user_id", "object_key", "oss_upload_id", "status").Result()
	if err != nil && err != redis.Nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	if len(vals) == 0 || (vals[0] == nil && vals[1] == nil) {
		_ = internal.RedisClient.Del(ctx, metaKey(req.UploadId)).Err()
		return &AbortResp{}, nil
	}

	ownerUserID, _ := vals[0].(string)
	objectKey, _ := vals[1].(string)
	ossUploadID, _ := vals[2].(string)
	statusStr, _ := vals[3].(string)
	if ownerUserID == "" {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}
	if err := ensureMultipartOwner(ctx, ownerUserID); err != nil {
		return nil, err
	}
	if statusStr != "completed" && objectKey != "" && ossUploadID != "" {
		_ = internal.MinIOClient.MinIOMultipartAbort(ctx, objectKey, ossUploadID)
	}
	_ = internal.RedisClient.Del(ctx, metaKey(req.UploadId)).Err()
	return &AbortResp{}, nil
}

func (s FileServer) Status(ctx context.Context, req *StatusReq) (*StatusResp, error) {
	if req == nil || strings.TrimSpace(req.UploadId) == "" {
		return nil, status.Error(codes.InvalidArgument, "missing upload_id")
	}

	vals, err := internal.RedisClient.HMGet(ctx, metaKey(req.UploadId), "user_id", "object_key", "oss_upload_id", "total_parts").Result()
	if err != nil {
		return nil, status.Errorf(codes.Internal, "redis HMGet meta: %v", err)
	}
	ownerUserID, _ := vals[0].(string)
	objectKey, _ := vals[1].(string)
	ossUploadID, _ := vals[2].(string)
	totalParts, _ := strconv.Atoi(fmt.Sprint(vals[3]))
	if ownerUserID == "" || objectKey == "" || ossUploadID == "" || totalParts <= 0 {
		return nil, status.Error(codes.NotFound, "upload_id not found or expired")
	}
	if err := ensureMultipartOwner(ctx, ownerUserID); err != nil {
		return nil, err
	}

	parts, err := listUploadedParts(ctx, objectKey, ossUploadID)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "list multipart parts: %v", err)
	}
	uploaded := make([]int32, 0, len(parts))
	for _, part := range parts {
		uploaded = append(uploaded, int32(part.PartNumber))
	}

	return &StatusResp{
		TotalParts:    int32(totalParts),
		UploadedParts: uploaded,
	}, nil
}
