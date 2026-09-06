package main

import (
	"context"
	"encoding/json"
	"fmt"
	"go_test/backword_part/model"
	storagecontrolpb "go_test/clouddisk_v2/storage_control/protobuf"
	"go_test/internal"
	"math"
	"strings"
	"time"

	"github.com/mark3labs/mcp-go/mcp"
	"github.com/mark3labs/mcp-go/server"
)

type ownedFile struct {
	FileID      uint
	Filename    string
	FileHash    string
	FileSize    int64
	Status      string
	ScanDetail  string
	ContentType string
	ObjectKey   string
}

func registerTools(s *server.MCPServer, clients *serviceClients, timeout time.Duration) {
	s.AddTool(accountUserinfoTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		claims, err := requireClaims(ctx)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		callCtx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()

		var account model.Account
		if err := internal.DB.WithContext(callCtx).First(&account, claims.UserID).Error; err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(map[string]any{
			"username":  account.Name,
			"email":     account.Email,
			"name":      account.Name,
			"mobile":    account.Mobile,
			"gender":    account.Gender,
			"createdAt": account.CreatedAt,
			"updatedAt": account.UpdatedAt,
		})
	})

	s.AddTool(fileShowTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		return presignOwnedFileTool(ctx, request, clients, timeout, true)
	})

	s.AddTool(fileDownloadTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		return presignOwnedFileTool(ctx, request, clients, timeout, false)
	})

	s.AddTool(fileQueryTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		claims, err := requireClaims(ctx)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		callCtx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()

		files, err := listOwnedFiles(callCtx, claims.UserID)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		items := make([]map[string]any, 0, len(files))
		for _, file := range files {
			items = append(items, fileItemJSON(file))
		}
		return newJSONResult(map[string]any{
			"status":   0,
			"message":  "ok",
			"filelist": items,
		})
	})

	s.AddTool(fileResolveHashTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		claims, err := requireClaims(ctx)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		filename, err := request.RequireString("filename")
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		callCtx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()

		file, err := loadOwnedFile(callCtx, claims.UserID, filename, "", 0)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(fileItemJSON(file))
	})
}

func presignOwnedFileTool(ctx context.Context, request mcp.CallToolRequest, clients *serviceClients, timeout time.Duration, inline bool) (*mcp.CallToolResult, error) {
	claims, err := requireClaims(ctx)
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}
	filename, err := request.RequireString("filename")
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}
	fileHash, err := request.RequireString("file_hash")
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}
	fileSize, err := requireInt32(request, "file_size")
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}

	callCtx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	file, err := loadOwnedFile(callCtx, claims.UserID, filename, fileHash, int64(fileSize))
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}
	if file.Status != model.FileSuccess {
		return mcp.NewToolResultError(fmt.Sprintf("file is not available: status=%s", file.Status)), nil
	}
	if clients == nil || clients.storageControl == nil {
		return mcp.NewToolResultError("storage_control client unavailable"), nil
	}
	objectKey := strings.TrimSpace(file.ObjectKey)
	if objectKey == "" {
		objectKey = "files/" + strings.TrimSpace(file.FileHash)
	}
	resp, err := clients.storageControl.PresignGet(callCtx, &storagecontrolpb.PresignGetReq{
		ObjectKey:         objectKey,
		Filename:          file.Filename,
		ContentType:       file.ContentType,
		InlineDisposition: inline,
		ExpiresSeconds:    int64((10 * time.Minute).Seconds()),
	})
	if err != nil {
		return mcp.NewToolResultError(err.Error()), nil
	}
	key := "download_url"
	if inline {
		key = "preview_url"
	}
	return newJSONResult(map[string]any{
		"status":   0,
		"message":  resp.Url,
		key:        resp.Url,
		"filename": file.Filename,
	})
}

func listOwnedFiles(ctx context.Context, userID int32) ([]ownedFile, error) {
	var files []ownedFile
	result := internal.DB.WithContext(ctx).Raw(
		"select f.id as file_id, uf.name as filename, f.sha1 as file_hash, f.size as file_size, f.status, f.scan_detail, f.content_type, f.object_key "+
			"from user_files uf join files f on f.id = uf.file_id and f.deleted_at is null "+
			"where uf.account_id = ? and uf.deleted_at is null order by uf.id desc",
		uint(userID)).Scan(&files)
	return files, result.Error
}

func loadOwnedFile(ctx context.Context, userID int32, filename, fileHash string, fileSize int64) (ownedFile, error) {
	query := "select f.id as file_id, uf.name as filename, f.sha1 as file_hash, f.size as file_size, f.status, f.scan_detail, f.content_type, f.object_key " +
		"from user_files uf join files f on f.id = uf.file_id and f.deleted_at is null " +
		"where uf.account_id = ? and uf.deleted_at is null and uf.name = ?"
	args := []any{uint(userID), filename}
	if strings.TrimSpace(fileHash) != "" {
		query += " and f.sha1 = ?"
		args = append(args, fileHash)
	}
	if fileSize > 0 {
		query += " and f.size = ?"
		args = append(args, fileSize)
	}
	query += " limit 1"

	var file ownedFile
	result := internal.DB.WithContext(ctx).Raw(query, args...).Scan(&file)
	if result.Error != nil {
		return ownedFile{}, result.Error
	}
	if result.RowsAffected == 0 {
		return ownedFile{}, fmt.Errorf("file not found or not owned")
	}
	return file, nil
}

func fileItemJSON(file ownedFile) map[string]any {
	return map[string]any{
		"file_id":      file.FileID,
		"filename":     file.Filename,
		"filehash":     file.FileHash,
		"file_hash":    file.FileHash,
		"filesize":     file.FileSize,
		"file_size":    file.FileSize,
		"status":       file.Status,
		"scan_detail":  file.ScanDetail,
		"content_type": file.ContentType,
		"object_key":   file.ObjectKey,
	}
}

func accountUserinfoTool() mcp.Tool {
	return mcp.NewTool("account_userinfo",
		mcp.WithDescription("展示用户的个人信息"),
	)
}

func fileShowTool() mcp.Tool {
	return mcp.NewTool("file_show",
		mcp.WithDescription("展示文件内容，返回一个inline url"),
		mcp.WithString("filename", mcp.Required(), mcp.Description("Original file name")),
		mcp.WithString("file_hash", mcp.Required(), mcp.Description("File SHA1 hash")),
		mcp.WithNumber("file_size", mcp.Required(), mcp.Description("File size (int32)")),
	)
}

func fileDownloadTool() mcp.Tool {
	return mcp.NewTool("file_download",
		mcp.WithDescription("下载文件，返回一个下载url"),
		mcp.WithString("filename", mcp.Required(), mcp.Description("Original file name")),
		mcp.WithString("file_hash", mcp.Required(), mcp.Description("File SHA1 hash")),
		mcp.WithNumber("file_size", mcp.Required(), mcp.Description("File size (int32)")),
	)
}

func fileQueryTool() mcp.Tool {
	return mcp.NewTool("file_query",
		mcp.WithDescription("返回用户拥有的全部文件信息"),
	)
}

func fileResolveHashTool() mcp.Tool {
	return mcp.NewTool("file_resolve_hash",
		mcp.WithDescription("返回用户需要文件的hash/sha1值"),
		mcp.WithString("filename", mcp.Required(), mcp.Description("User file name (exact match)")),
	)
}

func newJSONResult(data any) (*mcp.CallToolResult, error) {
	result, err := mcp.NewToolResultJSON(data)
	if err != nil {
		fallback, _ := json.Marshal(map[string]any{
			"error": err.Error(),
		})
		return mcp.NewToolResultText(string(fallback)), nil
	}
	return result, nil
}

func requireInt32(request mcp.CallToolRequest, key string) (int32, error) {
	raw, err := request.RequireFloat(key)
	if err != nil {
		return 0, err
	}
	if math.Trunc(raw) != raw {
		return 0, fmt.Errorf("%s must be an integer", key)
	}
	if raw < -2147483648 || raw > 2147483647 {
		return 0, fmt.Errorf("%s out of int32 range", key)
	}
	return int32(raw), nil
}
