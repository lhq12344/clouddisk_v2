# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

CloudDisk V2 — polyglot cloud storage platform. Go microservices (gRPC) + C++ gateway + React/TypeScript frontend + OpenResty reverse proxy. See AGENTS.md for detailed code style conventions and patterns.

## Architecture

```
Client → OpenResty (:2024) → C++ Gateway (Drogon) → Go gRPC Services
                                ├── account_srv — auth, signup, JWT
                                ├── file_srv — upload/download/delete, multipart
                                ├── AI_srv — AI features
                                └── mcp_server — MCP tool server

file_srv → Kafka (outbox pattern) → store_srv (consumer) → MinIO/AliOSS
email_srv (C++) ← Kafka — email notifications
```

Infrastructure: Nacos (config center) → Viper, Consul (service discovery), Kafka (async events), MySQL/GORM, Redis, MinIO/AliOSS.

## Go Module Path

The Go module is named `go_test` (in go.mod). All imports use this prefix: `go_test/internal`, `go_test/backword_part/...`.

## Build & Run

### Go Backend (Go 1.24)
```bash
go build -o bin/account_srv ./backword_part/account_server/account_srv/
go build -o bin/file_srv    ./backword_part/file_server/file_srv/
go build -o bin/store_srv   ./other_srv/store_srv/

go run ./backword_part/account_server/account_srv/
go run ./backword_part/file_server/file_srv/
go run ./other_srv/store_srv/
```

### Frontend (React 19 + Vite 6)
```bash
cd forward_part/static
npm install
npm run dev       # :3000
npm run build     # → dist/
```

### C++ Gateway (Drogon)
```bash
cd forward_part/gateway && mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### C++ Email Service
```bash
cd other_srv/email_srv && mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

### Protobuf Generation
```bash
protoc --go_out=. --go-grpc_out=. proto/account_srv/account.proto
protoc --go_out=. --go-grpc_out=. proto/file_srv/file.proto
protoc --go_out=. --go-grpc_out=. proto/AI_srv/ai.proto
```

## Testing

```bash
go test ./...                                          # all Go tests
go test ./internal/ -run TestFunctionName -v           # single test
go test ./backword_part/... -run TestName -v -count=1  # specific package
go vet ./...                                           # lint

# Performance test
./perf_test.sh -u http://localhost:8080/health -d 15 -q 200 -c 20

# Multipart upload test
go run test/mputest.go -addr 127.0.0.1:50051 -file ./big.bin -user_id 1001 -parallel 4
```

## Key Directory Layout

- `backword_part/` — Go backend services (account_server, file_server, AI_server, mcp_server)
- `forward_part/gateway/` — C++ gateway (Drogon framework, controllers/, filters/, ArcCache/)
- `forward_part/static/` — React frontend (src/App.tsx, components/, services/api.ts, types.ts)
- `other_srv/store_srv/` — Kafka consumer → MinIO/local storage
- `other_srv/email_srv/` — C++ email service
- `internal/` — Go shared utilities (config, DB, Redis, MinIO, JWT, Kafka, Consul clients)
- `proto/` — Protobuf definitions (account_srv/, file_srv/, AI_srv/)
- `backword_part/model/` — GORM database models (Account, File, UserFile, Outbox, Inbox)
- `scripts/` — Kubernetes deployment manifests

## Important Conventions

- Chinese comments are common and acceptable throughout the codebase
- Go error messages use `[FuncName]` prefix: `fmt.Errorf("[Download]account not found")`
- Global clients initialized in `init()`: `internal.DB`, `internal.RedisClient`, `internal.MinIOClient`, `internal.KafkaProducer`, `internal.ConsulClient`
- Config flows from Nacos → Viper → `internal.ViperConf` global struct with hot-reload
- Each gRPC service follows: `ListenAutoPort()` → `grpc.NewServer()` → register service → Consul register → `Serve()` → `ElegantExit()`
- Frontend uses `ApiService` singleton in `services/api.ts`, JWT in `localStorage('oss_token')`
- Async file ops use outbox pattern: write Outbox row in DB transaction → Kafka → store_srv consumer with Inbox idempotency
- Protobuf Go package convention: `option go_package = "clouddisk_v2/<service>/protobuf;<alias>pb"`
