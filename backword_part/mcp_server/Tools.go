package main

import (
	"context"
	"encoding/json"
	"fmt"
	accountpb "go_test/backword_part/account_server/account_srv/protobuf"
	filepb "go_test/backword_part/file_server/file_srv/protobuf"
	"math"
	"time"

	"github.com/mark3labs/mcp-go/mcp"
	"github.com/mark3labs/mcp-go/server"
)

func registerTools(s *server.MCPServer, clients *serviceClients, timeout time.Duration) {

	s.AddTool(accountUserinfoTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		claims, err := requireClaims(ctx)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		callCtx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()

		resp, err := clients.account.Userinfo(callCtx, &accountpb.ReqUserinfo{
			ID:       claims.UserID,
			Username: claims.Username,
		})
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(resp)
	})

	s.AddTool(fileShowTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
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

		resp, err := clients.file.Showfile(callCtx, &filepb.Reqshowfile{
			Username: claims.Username,
			Userid:   fmt.Sprintf("%d", claims.UserID),
			Filename: filename,
			Filehash: fileHash,
			FileSize: fileSize,
		})
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(resp)
	})

	s.AddTool(fileDownloadTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
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

		resp, err := clients.file.Filedowm(callCtx, &filepb.ReqFileDown{
			Username: claims.Username,
			Userid:   fmt.Sprintf("%d", claims.UserID),
			Filename: filename,
			Filehash: fileHash,
			FileSize: fileSize,
		})
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(resp)
	})

	s.AddTool(fileQueryTool(), func(ctx context.Context, request mcp.CallToolRequest) (*mcp.CallToolResult, error) {
		claims, err := requireClaims(ctx)
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}

		callCtx, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()

		resp, err := clients.file.Filequeryinfo(callCtx, &filepb.ReqFileQuery{
			Username: claims.Username,
			Userid:   fmt.Sprintf("%d", claims.UserID),
		})
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(resp)
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

		resp, err := clients.file.ResolveFileHash(callCtx, &filepb.ReqResolveFileHash{
			Userid:   fmt.Sprintf("%d", claims.UserID),
			Filename: filename,
		})
		if err != nil {
			return mcp.NewToolResultError(err.Error()), nil
		}
		return newJSONResult(resp)
	})

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
