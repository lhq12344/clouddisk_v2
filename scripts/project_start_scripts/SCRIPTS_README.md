# CloudDisk V2 服务管理脚本

## 脚本说明

### 1. start_all.sh - 一键启动所有服务

启动 CloudDisk V2 的当前 Core 拓扑服务，包括：
- OpenResty/Nginx 反向代理
- C++ Gateway (Drogon)
- Go 运行角色 (outbox_relay, storage_control, store_srv/storage-worker, AI_srv, mcp_srv)
- React 前端 (Vite 开发服务器)

**使用方法：**
```bash
./start_all.sh
```

**功能：**
- 自动检查依赖 (Go, OpenResty/Nginx, Node.js)
- 基础设施检查失败时输出 Kubernetes 侧诊断信息
- 本机缺少 OpenResty/Nginx 但存在 Docker 时，自动使用 OpenResty 容器启动反向代理
- 如果 C++ Gateway 未编译，自动编译
- 按顺序启动所有服务
- 将日志输出到 `log/` 目录
- 将 PID 保存到 `.pids/` 目录
- 显示服务状态和访问地址

### 2. stop_all.sh - 停止所有服务

优雅地停止所有正在运行的服务。

**使用方法：**
```bash
./stop_all.sh
```

**功能：**
- 读取 `.pids/` 目录中的 PID 文件
- 依次停止所有服务
- 清理 PID 文件

### 3. status.sh - 查看服务状态

查看所有服务的运行状态、资源使用情况和最近日志。

**使用方法：**
```bash
./status.sh
```

**显示信息：**
- 服务运行状态 (运行中/已停止/未启动)
- PID、内存使用、CPU 使用、运行时间
- 最近 3 行日志
- 端口监听情况

### 4. verify_core_source_suite.ps1 - Core 源码验收套件

在不启动中间件的情况下执行当前 Core API 迁移的源码级 gate：HTTP contract、cutover readiness、legacy removal、OpenResty 路由、Core source invariants、Gateway Core compile probe、Go test/vet/build、前端 build 和 whitespace 检查。默认会尝试 CMake configure；如果本机缺 C++ SDK（例如 Drogon CMake package），CMake 会用可执行提示 fail fast，套件会标记为 `BLOCKED` 而不是误报源码失败。

其中 `verify_core_source_invariants.mjs` 会防止关键迁移约束回退：旧 account/file 服务重新进入运行时路径、legacy flag 不再 fail-closed、Storage Control RPC 面缺失、outbox 写入不幂等、前端重新走字节代理上传等。

Go 运行角色构建会逐包执行，并输出到 `.cache/source-suite-bins/`，使用低并发和去调试符号参数，避免 Windows 本地链接阶段因内存不足误报失败。

Go 单元测试默认不会连接 Nacos/MySQL/Redis/Kafka/MinIO；只有显式设置 `CLOUDDISK_TEST_BOOTSTRAP=1` 时，测试二进制才会执行外部基础设施 bootstrap。

源码套件也会运行 `verify_core_runtime_smoke_contract.mjs`，在不启动中间件的情况下静态确认 runtime smoke 仍覆盖 health/ready、旧上传代理 fail-closed、multipart 直传、complete/query、扫描成功后的 download/preview 和清理分支。

性能验收的 baseline 脚本由 `verify_core_baseline_contract.mjs` 静态保护，确保 PowerShell/Bash runner 持续覆盖 health、userinfo、file query、init/presign/complete，并保留 JWT/自动登录和 p50/p90/p99 输出。

PowerShell 运维脚本由 `verify_powershell_script_syntax.ps1` 做 AST 语法检查，并已纳入源码套件，避免 runtime 前置/smoke/baseline 脚本带语法错误进入后续阶段。

源码套件还会运行 `verify_no_committed_secrets.mjs`，防止已移除的 SMTP app password、私钥、真实云 AK、非空 SMTP pass 或直接日志打印 secret 回到仓库。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_source_suite.ps1
```

如果要在 CI 或完整 C++ 环境中要求 gateway 必须完成 CMake configure：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_source_suite.ps1 -RequireCmake
```

### 5. verify_core_runtime_prereqs.ps1 - Core 运行时前置检查

在执行 runtime smoke / baseline 前，检查本机工具链、Drogon CMake 包、配置文件、关键 TCP 端口、`BASE_URL` 的 `/health` HTTP 响应，以及 JWT 或自动登录凭据是否已准备好。默认发现缺口会返回非零；本地盘点可使用 `-WarnOnly`。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_prereqs.ps1 -WarnOnly
```

自动化/CI 可加 `-Json` 获取结构化检查结果：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_prereqs.ps1 -WarnOnly -Json
```

源码套件会运行 `verify_core_runtime_prereqs_selftest.ps1`，保证 `-Json` 输出可解析且包含 smoke/baseline 所需的关键检查项。

如果要直接检查 gateway 而不是 OpenResty 入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_prereqs.ps1 -WarnOnly -BaseUrl http://10.42.0.1:8080
```

### 6. verify_core_runtime_smoke.ps1 - Core 运行时 smoke

中间件和 gateway 启动后，验证 no-legacy Core 主链路：`/health`、`/ready`、旧上传代理 fail-closed、multipart presigned PUT、`CompleteMultipart`、`/file/query`。如果文件仍处于 `pending_scan`，脚本会跳过删除清理；加 `-WaitForScanSuccess` 后会在扫描成功后继续验证 download/preview 并删除测试文件关系。

```powershell
$env:BASE_URL = 'http://127.0.0.1:2024'
$env:JWT = '<valid token>'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_smoke.ps1 -RequireJwt
```

也可以不用预先准备 JWT，改用现有测试账户自动登录：

```powershell
$env:BASE_URL = 'http://127.0.0.1:2024'
$env:SMOKE_SIGNIN_USERNAME = '<username>'
$env:SMOKE_SIGNIN_PASSWORD = '<password>'
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_smoke.ps1 -RequireJwt
```

如需等病毒扫描完成后再验证 download/preview 预签名：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/project_start_scripts/verify_core_runtime_smoke.ps1 -RequireJwt -WaitForScanSuccess
```

如需验证跨用户越权拒绝，额外设置 `OTHER_JWT`，或设置 `SMOKE_OTHER_SIGNIN_USERNAME` / `SMOKE_OTHER_SIGNIN_PASSWORD` 让脚本自动登录第二个用户；脚本会尝试用第二用户操作第一用户的 upload session（status、presign、complete、abort）以及下载、预览、删除第一用户上传的文件，并要求这些请求不能返回 200。

完整权限验收建议加 `-RequireOtherJwt`，这样第二用户凭据缺失时脚本会失败，而不是把跨用户越权矩阵记为 skipped。

## 目录结构

```
clouddisk_v2/
├── start_all.sh          # 启动脚本
├── stop_all.sh           # 停止脚本
├── status.sh             # 状态查看脚本
├── SCRIPTS_README.md     # 本文件
├── log/                  # 日志目录 (自动创建)
│   ├── gateway.log
│   ├── outbox_relay.log
│   ├── storage_control.log
│   ├── store_srv.log
│   ├── ai_srv.log
│   ├── mcp_srv.log
│   └── frontend.log
└── .pids/                # PID 文件目录 (自动创建)
    ├── gateway.pid
    ├── outbox_relay.pid
    ├── storage_control.pid
    ├── store_srv.pid
    ├── ai_srv.pid
    ├── mcp_srv.pid
    └── frontend.pid
```

## 服务端口

| 服务 | 端口 | 说明 |
|------|------|------|
| OpenResty/Nginx | 2024 | 反向代理入口 |
| C++ Gateway | 38080 | HTTP 网关（本地配置；代码兜底 8080） |
| React 前端 | 3000 | Vite 开发服务器 |
| storage_control | 动态端口/Consul | 窄对象存储控制面 gRPC |
| outbox_relay | - | Outbox 到 Kafka 的独立投递进程 |
| store_srv | - | Storage worker / Kafka 消费者 |
| AI_srv | 动态端口/Consul | AI gRPC 服务 |
| mcp_srv | 50053 | MCP 工具服务 |

## 前置依赖

在使用脚本前，请确保以下服务已启动：

1. **Nacos** - 配置中心
2. **Consul** - 服务注册与发现
3. **Kafka** - 消息队列
4. **MySQL** - 数据库
5. **Redis** - 缓存
6. **MinIO** 或 **阿里云 OSS** - 对象存储

邮件验证码服务的 SMTP 密码不再写入 `other_srv/email_srv/email_config.json`。本地或演练运行前通过环境变量注入：

```bash
export EMAIL_SMTP_PASS='<smtp app password>'
```

可选覆盖项还包括 `EMAIL_SMTP_URL`、`EMAIL_SMTP_USER`、`EMAIL_SMTP_FROM` 和 `EMAIL_SMTP_FROM_NAME`。

## 常见问题

### 1. OpenResty/Nginx 启动失败

检查配置文件路径是否正确：
```bash
cat forward_part/config/nginx/nginx.conf
```

确保路径已更新为当前项目路径。

如果本机没有安装 OpenResty/Nginx，但安装了 Docker，启动脚本会自动回退到：
`swr.cn-north-4.myhuaweicloud.com/ddn-k8s/docker.io/uusec/openresty-manager:latest`

### 2. C++ Gateway 编译失败

检查依赖：
```bash
# 安装 Drogon 依赖
sudo apt-get install libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev
```

### 3. Go 服务启动失败

检查 Go 环境：
```bash
go version
go env GOROOT GOPATH
```

查看日志：
```bash
tail -f log/outbox_relay.log
tail -f log/storage_control.log
tail -f log/store_srv.log
```

### 4. 前端启动失败

安装依赖：
```bash
cd forward_part/static
npm install
```

### 5. 端口被占用

查看端口占用：
```bash
netstat -tuln | grep -E "2024|3000|38080|50053"
```

停止占用端口的进程或修改配置文件中的端口。

### 6. Nacos 报 `Unknown database 'nacos_config'`

这是 Nacos 元数据库没有初始化，不是单纯端口没开。

```bash
mysql -h127.0.0.1 -P30306 -uroot -p123456 < /home/lihaoqian/project/clouddisk_v2/scripts/sql/nacos-3.1.sql
/home/lihaoqian/project/k8s/bin/k8s-stack.sh stop nacos
/home/lihaoqian/project/k8s/bin/k8s-stack.sh start nacos
```

### 7. Consul 报 `server_rejoin_age_max`

这是旧数据目录导致的拒绝重连。开发环境可以直接重建数据卷：

```bash
/home/lihaoqian/project/k8s/bin/k8s-stack.sh stop consul
kubectl delete pvc -n infra consul-data-consul-0
/home/lihaoqian/project/k8s/bin/k8s-stack.sh start consul
```

## 日志查看

### 实时查看所有日志
```bash
tail -f log/*.log
```

### 查看特定服务日志
```bash
tail -f log/gateway.log
tail -f log/storage_control.log
tail -f log/outbox_relay.log
```

### 查看错误日志
```bash
grep -i error log/*.log
```

## 开发建议

1. **首次启动**：先确保所有依赖服务 (Nacos, Consul, Kafka 等) 已启动
2. **调试模式**：可以单独启动某个服务进行调试，而不使用脚本
3. **日志监控**：使用 `tail -f log/*.log` 实时监控所有服务日志
4. **资源监控**：使用 `./status.sh` 定期检查服务状态和资源使用

## 手动启动单个服务

如果需要单独启动某个服务进行调试：

```bash
# Go 服务
go run ./backword_part/outbox_relay/
go run ./backword_part/storage_control/
go run ./other_srv/store_srv/
go run ./backword_part/AI_server/
go run ./backword_part/mcp_server/

# C++ Gateway
cd forward_part/gateway/build && ./gateway

# 前端
cd forward_part/static && npm run dev

# OpenResty
openresty -c forward_part/config/nginx/nginx.conf -p forward_part/config/nginx/
```

## 更新日志

- 2026-03-04: 初始版本，支持一键启动/停止/状态查看
- 2026-03-22: 基础设施检查补充 K8s 诊断，明确 Nacos/Consul 常见故障修复步骤
