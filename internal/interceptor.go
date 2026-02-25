package internal

import (
	"context"

	"go.uber.org/zap"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"
)

type ctxKeyRequestID struct{}

const metadataKeyRequestID = "x-request-id"

// RequestIDUnaryInterceptor 从 gRPC metadata 提取 x-request-id 注入 context
func RequestIDUnaryInterceptor() grpc.UnaryServerInterceptor {
	return func(ctx context.Context, req any, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (any, error) {
		ctx = extractRequestID(ctx)
		return handler(ctx, req)
	}
}

// RequestIDStreamInterceptor 流式 RPC 拦截器（如 UploadPart）
func RequestIDStreamInterceptor() grpc.StreamServerInterceptor {
	return func(srv any, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
		ctx := extractRequestID(ss.Context())
		return handler(srv, &wrappedStream{ServerStream: ss, ctx: ctx})
	}
}

// RequestIDFromContext 从 context 取 request_id
func RequestIDFromContext(ctx context.Context) string {
	if rid, ok := ctx.Value(ctxKeyRequestID{}).(string); ok {
		return rid
	}
	return ""
}

// LoggerWithRID 返回带 request_id 字段的 zap.Logger
func LoggerWithRID(ctx context.Context, baseLogger *zap.Logger) *zap.Logger {
	rid := RequestIDFromContext(ctx)
	if rid == "" {
		return baseLogger
	}
	return baseLogger.With(zap.String("request_id", rid))
}

func extractRequestID(ctx context.Context) context.Context {
	md, ok := metadata.FromIncomingContext(ctx)
	if !ok {
		return ctx
	}
	vals := md.Get(metadataKeyRequestID)
	if len(vals) == 0 || vals[0] == "" {
		return ctx
	}
	return context.WithValue(ctx, ctxKeyRequestID{}, vals[0])
}

// wrappedStream 包装 ServerStream 以替换 context
type wrappedStream struct {
	grpc.ServerStream
	ctx context.Context
}

func (w *wrappedStream) Context() context.Context {
	return w.ctx
}
