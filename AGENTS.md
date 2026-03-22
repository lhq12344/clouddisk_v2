# AGENTS.md — CloudDisk V2

Polyglot cloud storage platform: Go microservices (gRPC), C++ gateway/email service, React+TypeScript frontend, OpenResty (Nginx+Lua) reverse proxy. Deployed on Kubernetes with Consul service discovery, Nacos config center, Kafka event bus, MinIO/AliOSS object storage, MySQL (GORM), Redis.

## Architecture Overview

```
Client → OpenResty (:2024) → Go Gateway (Consul-discovered)
                                ├── account_srv (gRPC) — auth, signup, JWT
                                ├── file_srv    (gRPC) — upload/download/delete, multipart
                                ├── AI_srv      (gRPC) — AI features
                                └── mcp_server  (MCP)  — tool server for AI agents

file_srv → Kafka (outbox pattern) → store_srv (consumer) → MinIO/AliOSS
email_srv (C++) ← Kafka — email notifications
```

## Build & Run Commands

### Go Backend (module: `go_test`, Go 1.24)

```bash
# Build individual services
go build -o bin/account_srv ./backword_part/account_server/account_srv/
go build -o bin/file_srv    ./backword_part/file_server/file_srv/
go build -o bin/ai_srv      ./backword_part/AI_server/
go build -o bin/mcp_server  ./backword_part/mcp_server/
go build -o bin/store_srv   ./other_srv/store_srv/

# Run a service directly
go run ./backword_part/account_server/account_srv/
go run ./backword_part/file_server/file_srv/
go run ./other_srv/store_srv/

# Run all Go tests
go test ./...

# Run a single test file or function
go test ./internal/ -run TestFunctionName -v
go test ./backword_part/... -run TestName -v -count=1

# Vet / lint
go vet ./...
```

### Frontend (React 19 + Vite 6, in `forward_part/static/`)

```bash
cd forward_part/static
npm install
npm run dev       # Dev server on :3000
npm run build     # Production build → dist/
npm run preview   # Preview production build
```

### C++ Email Service (C++17, CMake, in `other_srv/email_srv/`)

```bash
cd other_srv/email_srv
mkdir -p build && cd build
cmake .. && make -j$(nproc)
# Requires: librdkafka-dev, hiredis-dev, libcurl-dev, nacos-cli SDK
```

### C++ Gateway (forward_part/gateway/)

```bash
cd forward_part/gateway
mkdir -p build && cd build
cmake .. && make -j$(nproc)

# Run tests
ctest --output-on-failure
```

### Protobuf Code Generation

```bash
# From proto/ directory — generate Go stubs
protoc --go_out=. --go-grpc_out=. proto/account_srv/account.proto
protoc --go_out=. --go-grpc_out=. proto/file_srv/file.proto
protoc --go_out=. --go-grpc_out=. proto/AI_srv/ai.proto
```

### Performance Testing

```bash
./perf_test.sh -u http://localhost:8080/health -d 15 -q 200 -c 20
```

### Multipart Upload Testing

```bash
go run test/mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4
```

## Code Style & Conventions

### Go

- **Module path**: `go_test` (used in all import paths: `go_test/internal`, `go_test/backword_part/...`)
- **Imports**: stdlib → project packages → third-party. Grouped with blank lines between groups.
- **Naming**:
  - Files: snake_case (`user_service.go`, `auth_handler.go`)
  - PascalCase for exported types/funcs: `InitRedis()`, `ViperConf`, `MinIOClient`
  - camelCase for unexported: `genUploadID()`, `metaKey()`, `buildObjectKey()`
  - Constants: PascalCase (`FilePending`, `OutboxNew`, `TokenExpired`)
  - Interfaces: er suffix for implementations (`Repositoryer`, `Servicer`)
  - Config structs use `mapstructure` tags matching JSON/YAML keys
- **Error handling**: Return `(result, error)` pairs. Use `fmt.Errorf` with `[FuncName]` prefix for context: `fmt.Errorf("[Download]account not found")`. Use `github.com/pkg/errors` for wrapping. Check `gorm.ErrRecordNotFound` with `errors.Is()`.
- **Logging**: `go.uber.org/zap` structured logger. Use `log.Logger.Info/Error/Warn()` with `zap.String()`, `zap.Error()` fields. Two logger packages exist: `go_test/backword_part/log` and `go_test/internal` — each service uses its own.
- **Database**: GORM with MySQL. Models embed `gorm.Model`. Use `gorm:` struct tags. Transactions via `db.Transaction(func(tx *gorm.DB) error { ... })`. Upserts via `clause.OnConflict`.
- **Config**: Nacos config center → Viper unmarshaling → `internal.ViperConf` global. Hot-reload via `client.ListenConfig`. Init chain in `internal/viper_config_centre.go` `init()`.
- **Service pattern**: Each gRPC service has `main.go` with `ListenAutoPort()` → `grpc.NewServer()` → register service → register to Consul → `Serve()` → `ElegantExit()`.
- **Protobuf**: Server structs embed `Unimplemented*Server`. Implementation in `protobuf/*.go` files alongside generated code.
- **Global clients**: `internal.DB`, `internal.RedisClient`, `internal.MinIOClient`, `internal.KafkaProducer`, `internal.ConsulClient`, `internal.OssClient` — all initialized in `init()`.
- **Comments**: Mix of Chinese and English. Chinese comments are acceptable and common in this codebase.

### TypeScript / React Frontend

- **React 19** with functional components and hooks (`useState`, `useEffect`)
- **No state management library** — local state via `useState`, prop drilling
- **Path aliases**: `@/*` maps to project root (configured in tsconfig + vite)
- **Types**: Defined in `types.ts`. Use TypeScript interfaces (`interface FileItem`, `interface UserInfo`). Union types for state enums (`type AuthMode = 'signin' | 'signup' | 'verify'`)
- **API layer**: Singleton class `ApiService` in `services/api.ts`. All HTTP via `fetch()`. JWT token stored in `localStorage('oss_token')`. Authorization header: `Bearer ${token}`
- **Styling**: Tailwind CSS utility classes inline in JSX. Custom CSS classes: `glass-panel`, `neon-text`, `neon-border`. Dark/light theme via `data-theme` attribute on `<body>`
- **Error handling**: try/catch with `alert()` for user-facing errors, `console.error()` for logging
- **File structure**: `App.tsx` (main), `components/` (AuthPage, UploadManager), `services/api.ts`, `types.ts`
- **Component pattern**: `const Component: React.FC = () => { ... }` or `const Component: React.FC<Props> = ({ ... }) => { ... }`
- **File naming**: Components use PascalCase (`AuthPage.tsx`), hooks use camelCase with `use` prefix (`useAuth.ts`), utilities use camelCase (`formatBytes.ts`)
- **Imports**: ES modules with relative paths (e.g., `import { api } from './services/api'`). No absolute paths or CommonJS.
- **Formatting**: 2 spaces indentation for TSX files. No strict line length limit. Semicolons used.

### Protobuf

- **Syntax**: proto3
- **Go package**: `option go_package = "clouddisk_v2/<service>/protobuf;<alias>pb"` (e.g., `accountpb`, `filepb`)
- **Message naming**: PascalCase with `Req`/`Resp` prefix (`ReqSignin`, `RespFileQuery`)
- **Service naming**: camelCase service name, mixed case RPCs
- **Field naming**: snake_case for proto fields (`user_id`, `file_name`)
- **Enum naming**: PascalCase enum name, UPPER_SNAKE_CASE values

### C++ (Email Service & Gateway)

- **Standard**: C++17
- **Naming**: snake_case for functions/variables, PascalCase for classes, UPPER_SNAKE_CASE for constants
- **Error handling**: Exceptions for recoverable errors, error codes for performance-critical paths
- **Memory management**: Smart pointers (`std::unique_ptr`, `std::shared_ptr`) preferred over raw pointers

### Nginx / Lua (OpenResty)

- Listens on `:2024` (main app), `:2025` (file preview/download)
- Consul-based dynamic upstream via Lua (`lua/consul_service.lua`, `lua/balancer.lua`)
- CORS headers added globally. Preflight returns 204.
- Request ID generated in `access_by_lua_block` and forwarded as `X-Request-Id`
- **Lua naming**: snake_case for variables/functions, UPPER_SNAKE_CASE for constants

## Testing Guidelines

### Go Testing Patterns

- Test files: `*_test.go` suffix in same package
- Test functions: `TestFunctionName(t *testing.T)` pattern
- Use `testify/assert` for assertions
- Use `testify/mock` for mocking dependencies
- Table-driven tests preferred for multiple test cases:

```go
tests := []struct {
    name    string
    input   Type
    want    Type
    wantErr bool
}{
    {"case1", input1, want1, false},
    {"case2", input2, want2, true},
}
for _, tt := range tests {
    t.Run(tt.name, func(t *testing.T) {
        // test logic
    })
}
```

### Frontend Testing Patterns

- Use Vitest for unit tests
- Use React Testing Library for component tests
- Test files: `*.test.tsx` or `*.spec.tsx`
- Mock API calls with MSW (Mock Service Worker)

## Key Patterns to Follow

1. **New gRPC service**: Copy existing service pattern from `backword_part/account_server/` — create `main.go` with `ListenAutoPort`, register to Consul, implement proto interface
2. **New API endpoint**: Add proto message/RPC → regenerate → implement in `protobuf/*.go` → add Nginx location block
3. **New frontend feature**: Add types to `types.ts`, API method to `services/api.ts`, component in `components/`
4. **Async file operations**: Use outbox pattern — write to `Outbox` table in transaction, dispatcher sends to Kafka, `store_srv` consumer processes
5. **Error codes in gRPC responses**: `Code: 0` = success, non-zero = error. Error messages in `Message` field. Custom errors in `custom_error` package.

## Infrastructure Dependencies

| Service | Purpose                           | Config Source                         |
| ------- | --------------------------------- | ------------------------------------- |
| MySQL   | Primary database (GORM)           | Nacos `clouddisk.json`                |
| Redis   | Cache, multipart upload state     | Nacos                                 |
| Consul  | Service discovery + health checks | Nacos                                 |
| Kafka   | Async event bus (outbox pattern)  | Nacos                                 |
| MinIO   | Object storage (primary)          | Nacos                                 |
| AliOSS  | Object storage (secondary)        | Nacos                                 |
| Nacos   | Configuration center              | Hardcoded in `viper_config_centre.go` |
