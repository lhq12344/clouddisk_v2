package storagecontrolpb

import (
	"context"
	"fmt"
	"mime"
	"net/url"
	"sort"
	"strings"
	"time"

	"go_test/internal"

	"github.com/minio/minio-go/v7"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

const (
	defaultPartPresignExpiry = 15 * time.Minute
	defaultGetPresignExpiry  = 10 * time.Minute
)

type StorageControlService struct {
	UnimplementedStorageControlServer
}

func requireObjectKey(objectKey string) (string, error) {
	objectKey = strings.TrimSpace(objectKey)
	if objectKey == "" {
		return "", status.Error(codes.InvalidArgument, "object_key is required")
	}
	return objectKey, nil
}

func requireMinIOClient() error {
	if internal.MinIOClient == nil || internal.MinIOClient.Client == nil {
		return status.Error(codes.Unavailable, "minio client is not initialized")
	}
	return nil
}

func durationOrDefault(seconds int64, fallback time.Duration) time.Duration {
	if seconds <= 0 {
		return fallback
	}
	return time.Duration(seconds) * time.Second
}

func detectMimeByName(filename, fallback string) string {
	if fallback = strings.TrimSpace(fallback); fallback != "" {
		return fallback
	}
	if ext := strings.ToLower(strings.TrimSpace(filename)); ext != "" {
		if dot := strings.LastIndex(ext, "."); dot >= 0 {
			if ct := mime.TypeByExtension(ext[dot:]); ct != "" {
				return ct
			}
		}
	}
	return "application/octet-stream"
}

func contentDisposition(filename string, inline bool) string {
	disposition := "attachment"
	if inline {
		disposition = "inline"
	}
	filename = strings.TrimSpace(filename)
	if filename == "" {
		return disposition
	}
	escaped := url.QueryEscape(filename)
	escaped = strings.ReplaceAll(escaped, "+", "%20")
	return fmt.Sprintf("%s; filename*=UTF-8''%s", disposition, escaped)
}

func (s *StorageControlService) InitiateMultipart(ctx context.Context, req *InitiateMultipartReq) (*InitiateMultipartResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	contentType := detectMimeByName(objectKey, req.ContentType)
	storageUploadID, err := internal.MinIOClient.MinIOMultipartInit(ctx, objectKey, contentType, req.Metadata)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "minio init multipart: %v", err)
	}
	return &InitiateMultipartResp{StorageUploadId: storageUploadID}, nil
}

func (s *StorageControlService) PresignPart(ctx context.Context, req *PresignPartReq) (*PresignPartResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	storageUploadID := strings.TrimSpace(req.StorageUploadId)
	if storageUploadID == "" || req.PartNumber <= 0 {
		return nil, status.Error(codes.InvalidArgument, "storage_upload_id and positive part_number are required")
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	expiry := durationOrDefault(req.ExpiresSeconds, defaultPartPresignExpiry)
	u, err := internal.MinIOClient.MinIOPresignMultipartPutURL(ctx, objectKey, storageUploadID, int(req.PartNumber), expiry)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "presign part: %v", err)
	}
	return &PresignPartResp{Url: u, ExpiresAt: time.Now().Add(expiry).Format(time.RFC3339)}, nil
}

func (s *StorageControlService) ListParts(ctx context.Context, req *ListPartsReq) (*ListPartsResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	storageUploadID := strings.TrimSpace(req.StorageUploadId)
	if storageUploadID == "" {
		return nil, status.Error(codes.InvalidArgument, "storage_upload_id is required")
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	parts, err := internal.MinIOClient.MinIOMultipartListParts(ctx, objectKey, storageUploadID)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "list parts: %v", err)
	}
	resp := &ListPartsResp{Parts: make([]*UploadedPart, 0, len(parts))}
	for _, part := range parts {
		resp.Parts = append(resp.Parts, &UploadedPart{PartNumber: int32(part.PartNumber), Etag: part.ETag})
	}
	return resp, nil
}

func (s *StorageControlService) CompleteMultipart(ctx context.Context, req *CompleteMultipartReq) (*CompleteMultipartResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	storageUploadID := strings.TrimSpace(req.StorageUploadId)
	if storageUploadID == "" || len(req.Parts) == 0 {
		return nil, status.Error(codes.InvalidArgument, "storage_upload_id and parts are required")
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	completeParts := make([]minio.CompletePart, 0, len(req.Parts))
	for _, part := range req.Parts {
		if part.PartNumber <= 0 || strings.TrimSpace(part.Etag) == "" {
			return nil, status.Error(codes.InvalidArgument, "all parts require part_number and etag")
		}
		completeParts = append(completeParts, minio.CompletePart{PartNumber: int(part.PartNumber), ETag: part.Etag})
	}
	sort.Slice(completeParts, func(i, j int) bool { return completeParts[i].PartNumber < completeParts[j].PartNumber })
	info, err := internal.MinIOClient.MinIOMultipartComplete(ctx, objectKey, storageUploadID, completeParts, req.ContentType, nil)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "complete multipart: %v", err)
	}
	return &CompleteMultipartResp{ObjectKey: objectKey, Etag: info.ETag}, nil
}

func (s *StorageControlService) AbortMultipart(ctx context.Context, req *AbortMultipartReq) (*AbortMultipartResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	storageUploadID := strings.TrimSpace(req.StorageUploadId)
	if storageUploadID == "" {
		return nil, status.Error(codes.InvalidArgument, "storage_upload_id is required")
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	if err := internal.MinIOClient.MinIOMultipartAbort(ctx, objectKey, storageUploadID); err != nil {
		return nil, status.Errorf(codes.Internal, "abort multipart: %v", err)
	}
	return &AbortMultipartResp{}, nil
}

func (s *StorageControlService) PresignGet(ctx context.Context, req *PresignGetReq) (*PresignGetResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	filename := strings.TrimSpace(req.Filename)
	if filename == "" {
		filename = objectKey
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	reqParams := make(url.Values)
	reqParams.Set("response-content-disposition", contentDisposition(filename, req.InlineDisposition))
	reqParams.Set("response-content-type", detectMimeByName(filename, req.ContentType))
	u, err := internal.MinIOClient.MinIOPresignGetURLWithParams(objectKey, durationOrDefault(req.ExpiresSeconds, defaultGetPresignExpiry), reqParams)
	if err != nil {
		return nil, status.Errorf(codes.Internal, "presign get: %v", err)
	}
	return &PresignGetResp{Url: u}, nil
}

func (s *StorageControlService) HeadObject(ctx context.Context, req *HeadObjectReq) (*HeadObjectResp, error) {
	if req == nil {
		return nil, status.Error(codes.InvalidArgument, "nil request")
	}
	objectKey, err := requireObjectKey(req.ObjectKey)
	if err != nil {
		return nil, err
	}
	if err := requireMinIOClient(); err != nil {
		return nil, err
	}
	info, err := internal.MinIOClient.Client.StatObject(ctx, internal.MinIOClient.Bucket, objectKey, minio.StatObjectOptions{})
	if err != nil {
		resp := minio.ToErrorResponse(err)
		if resp.Code == "NoSuchKey" || resp.Code == "NotFound" {
			return &HeadObjectResp{Exists: false}, nil
		}
		return nil, status.Errorf(codes.Internal, "head object: %v", err)
	}
	return &HeadObjectResp{Exists: true, Size: info.Size, Etag: info.ETag, ContentType: info.ContentType}, nil
}
