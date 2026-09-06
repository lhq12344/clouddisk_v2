package main

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	storagecontrolpb "go_test/clouddisk_v2/storage_control/protobuf"
	"go_test/internal"

	"github.com/mark3labs/mcp-go/server"
	"go.uber.org/zap"
	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
)

// ================= 认证 claims==================== ==========
type ctxKey string

const claimsKey ctxKey = "claims"

type Claims struct {
	UserID   int32
	Username string
}

func requireClaims(ctx context.Context) (*Claims, error) {
	v := ctx.Value(claimsKey)
	c, ok := v.(*Claims)
	if !ok || c == nil {
		return nil, fmt.Errorf("missing auth claims in context")
	}
	if c.UserID <= 0 || strings.TrimSpace(c.Username) == "" {
		return nil, fmt.Errorf("invalid auth claims")
	}
	return c, nil
}

// ========== 配置 ==========
const (
	defaultTransport      = "sse"
	defaultBaseURL        = ""     // 单实例直连：建议为空（返回 path，避免 URL 拼错）
	defaultBasePath       = "/mcp" // 你的静态挂载点
	defaultDialTimeout    = 5 * time.Second
	defaultRequestTimeout = 15 * time.Second
	defaultInitRetryCount = 10
	defaultInitRetryWait  = 2 * time.Second

	// SSE 保活：避免中间链路 idle 超时断开
	defaultKeepAlive         = true
	defaultKeepAliveInterval = 15 * time.Second
)

type config struct {
	transport      string
	httpAddr       string
	baseURL        string
	basePath       string
	storageAddr    string
	consulAddr     string
	dialTimeout    time.Duration
	requestTimeout time.Duration
}

type serviceClients struct {
	storageConn    *grpc.ClientConn
	storageControl storagecontrolpb.StorageControlClient
}

func (c *serviceClients) Close() {
	if c.storageConn != nil {
		_ = c.storageConn.Close()
	}
}

// ========== main ==========
func main() {
	rootCtx := context.Background()
	serviceID := "mcp_srv_1"

	cfg, clients, err := initDependenciesWithRetry()
	if err != nil {
		internal.Logger.Error("mcp dependency init failed", zap.Error(err))
		return
	}
	defer clients.Close()

	mcpServer := server.NewMCPServer(
		"clouddisk-backword",
		"1.0.0",
		server.WithToolCapabilities(true),
		server.WithRecovery(),
	)

	registerTools(mcpServer, clients, cfg.requestTimeout)

	if strings.EqualFold(cfg.transport, "stdio") {
		if err := server.ServeStdio(mcpServer); err != nil {
			internal.Logger.Error("mcp stdio server error", zap.Error(err))
		}
		return
	}

	// SSE options：关键是不要乱拼 full URL，单实例建议返回 path
	sseOpts := []server.SSEOption{
		server.WithStaticBasePath(cfg.basePath),

		// 让 server 返回的 message endpoint 是“相对路径”而不是 full URL
		server.WithUseFullURLForMessageEndpoint(false),

		// 保活，减少中间链路断流风险
		server.WithKeepAlive(defaultKeepAlive),
		server.WithKeepAliveInterval(defaultKeepAliveInterval),

		// 每个 HTTP 请求（/sse、/message）按 header 注入 claims
		server.WithSSEContextFunc(func(ctx context.Context, r *http.Request) context.Context {
			uidStr := strings.TrimSpace(r.Header.Get("X-User-Id"))
			uname := strings.TrimSpace(r.Header.Get("X-Username"))
			internal.Logger.Info("mcp incoming headers",
				zap.String("path", r.URL.Path),
				zap.String("x-user-id", uidStr),
				zap.String("x-username", uname),
			)
			if uidStr == "" || uname == "" {
				return ctx
			}
			uid64, err := strconv.ParseInt(uidStr, 10, 32)
			if err != nil || uid64 <= 0 {
				return ctx
			}
			cl := &Claims{UserID: int32(uid64), Username: uname}
			return context.WithValue(ctx, claimsKey, cl)
		}),
	}

	// 只有当你“必须”让 server 返回 full URL（给外部 client 用）才设置 baseURL
	if strings.TrimSpace(cfg.baseURL) != "" {
		sseOpts = append(sseOpts,
			server.WithBaseURL(cfg.baseURL),
			server.WithUseFullURLForMessageEndpoint(true),
		)
	}

	sseServer := server.NewSSEServer(mcpServer, sseOpts...)

	// 自定义 mux：同端口挂 /health + MCP
	mux := http.NewServeMux()
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte("ok"))
	})
	// 让 SSEServer 自己根据 basePath 路由 /mcp/sse 和 /mcp/message
	mux.Handle("/", sseServer)

	httpSrv := &http.Server{
		Addr:              cfg.httpAddr,
		Handler:           mux,
		ReadHeaderTimeout: 5 * time.Second,
		// 注意：不要给 SSE 配置 WriteTimeout/ReadTimeout（会把长连接掐掉）
	}

	// 优雅退出
	shutdown := make(chan os.Signal, 1)
	signal.Notify(shutdown, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-shutdown
		ctx, cancel := context.WithTimeout(rootCtx, 5*time.Second)
		defer cancel()

		internal.DeregisterService(serviceID)

		// 先关 SSE sessions（内部会关闭会话），再关 HTTP Server
		_ = sseServer.Shutdown(ctx)
		_ = httpSrv.Shutdown(ctx)
	}()

	host, port, splitErr := net.SplitHostPort(cfg.httpAddr)
	if splitErr != nil {
		internal.Logger.Error("split mcp listen address failed",
			zap.String("http_addr", cfg.httpAddr),
			zap.Error(splitErr),
		)
		return
	}

	portNum, convErr := strconv.Atoi(port)
	if convErr != nil {
		internal.Logger.Error("parse mcp listen port failed",
			zap.String("port", port),
			zap.Error(convErr),
		)
		return
	}

	if err := internal.RegisterGinService("mcp_srv", serviceID, host, portNum); err != nil {
		internal.Logger.Error("mcp consul register failed", zap.Error(err))
		return
	}

	internal.Logger.Info("mcp server listening",
		zap.String("address", cfg.httpAddr),
		zap.String("base_path", cfg.basePath),
		zap.String("base_url", cfg.baseURL),
	)

	// 用我们自己的 httpSrv 启动，而不是 sseServer.Start（避免 /health 无法挂载）
	if err := httpSrv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		internal.Logger.Error("mcp http server error", zap.Error(err))
	}
}

func initDependenciesWithRetry() (config, *serviceClients, error) {
	var (
		cfg     config
		clients *serviceClients
		err     error
	)

	for attempt := 1; attempt <= defaultInitRetryCount; attempt++ {
		cfg, err = loadConfig()
		if err == nil {
			clients, err = newServiceClients(cfg)
		}
		if err == nil {
			return cfg, clients, nil
		}

		internal.Logger.Warn("mcp dependency init retry",
			zap.Int("attempt", attempt),
			zap.Int("max_attempts", defaultInitRetryCount),
			zap.Duration("retry_after", defaultInitRetryWait),
			zap.Error(err),
		)

		if attempt < defaultInitRetryCount {
			time.Sleep(defaultInitRetryWait)
		}
	}

	return config{}, nil, err
}

// ========== 服务发现 / 配置 ==========
func FindServer(key string) (string, error) {
	srvHost, srvPort, err := internal.DiscoverService(key)
	if err != nil {
		internal.Logger.Error("find server failed", zap.String("key", key), zap.Error(err))
		return "", fmt.Errorf("discover service failed: %w", err)
	}
	return fmt.Sprintf("%s:%d", srvHost, srvPort), nil
}

func loadConfig() (config, error) {
	ip := internal.ViperConf.ConsulConfig.MCPSrv.Host
	port := internal.ViperConf.ConsulConfig.MCPSrv.Port
	addr := fmt.Sprintf("%v:%d", ip, port)

	storageADDR, err := FindServer("storage_control")
	if err != nil {
		return config{}, err
	}
	consulADDR := internal.ViperConf.ConsulConfig.Host + ":" + internal.ViperConf.ConsulConfig.Port

	cfg := config{
		transport:      defaultTransport,
		httpAddr:       addr,
		baseURL:        defaultBaseURL,
		basePath:       defaultBasePath,
		storageAddr:    storageADDR,
		consulAddr:     consulADDR,
		dialTimeout:    defaultDialTimeout,
		requestTimeout: defaultRequestTimeout,
	}
	cfg.basePath = normalizeBasePath(cfg.basePath)
	return cfg, nil
}

func normalizeBasePath(basePath string) string {
	basePath = strings.TrimSpace(basePath)
	if basePath == "" {
		return defaultBasePath
	}
	if !strings.HasPrefix(basePath, "/") {
		basePath = "/" + basePath
	}
	return strings.TrimRight(basePath, "/")
}

func newServiceClients(cfg config) (*serviceClients, error) {
	storageConn, err := dialGRPC(cfg.storageAddr, cfg.dialTimeout)
	if err != nil {
		return nil, fmt.Errorf("dial storage_control grpc: %w", err)
	}

	return &serviceClients{
		storageConn:    storageConn,
		storageControl: storagecontrolpb.NewStorageControlClient(storageConn),
	}, nil
}

func dialGRPC(addr string, timeout time.Duration) (*grpc.ClientConn, error) {
	ctx, cancel := context.WithTimeout(context.Background(), timeout)
	defer cancel()

	dialer := &net.Dialer{}

	return grpc.DialContext(
		ctx,
		addr,
		grpc.WithTransportCredentials(insecure.NewCredentials()),
		grpc.WithBlock(),
		grpc.WithContextDialer(func(ctx context.Context, target string) (net.Conn, error) {
			return dialer.DialContext(ctx, "tcp", target)
		}),
	)
}
