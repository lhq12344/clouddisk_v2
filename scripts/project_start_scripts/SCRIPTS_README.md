# CloudDisk V2 服务管理脚本

## 脚本说明

### 1. start_all.sh - 一键启动所有服务

启动 CloudDisk V2 的所有服务，包括：
- OpenResty/Nginx 反向代理
- C++ Gateway (Drogon)
- Go 微服务 (account_srv, file_srv, store_srv, AI_srv, mcp_srv)
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

## 目录结构

```
clouddisk_v2/
├── start_all.sh          # 启动脚本
├── stop_all.sh           # 停止脚本
├── status.sh             # 状态查看脚本
├── SCRIPTS_README.md     # 本文件
├── log/                  # 日志目录 (自动创建)
│   ├── gateway.log
│   ├── account_srv.log
│   ├── file_srv.log
│   ├── store_srv.log
│   ├── ai_srv.log
│   ├── mcp_srv.log
│   └── frontend.log
└── .pids/                # PID 文件目录 (自动创建)
    ├── gateway.pid
    ├── account_srv.pid
    ├── file_srv.pid
    ├── store_srv.pid
    ├── ai_srv.pid
    ├── mcp_srv.pid
    └── frontend.pid
```

## 服务端口

| 服务 | 端口 | 说明 |
|------|------|------|
| OpenResty/Nginx | 2024 | 反向代理入口 |
| C++ Gateway | 8080 | HTTP 网关 |
| React 前端 | 3000 | Vite 开发服务器 |
| account_srv | 50051 | gRPC 服务 (动态端口) |
| file_srv | 50052 | gRPC 服务 (动态端口) |
| store_srv | - | Kafka 消费者 |

## 前置依赖

在使用脚本前，请确保以下服务已启动：

1. **Nacos** - 配置中心
2. **Consul** - 服务注册与发现
3. **Kafka** - 消息队列
4. **MySQL** - 数据库
5. **Redis** - 缓存
6. **MinIO** 或 **阿里云 OSS** - 对象存储

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
tail -f log/account_srv.log
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
netstat -tuln | grep -E "2024|3000|8080"
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
tail -f log/account_srv.log
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
go run ./backword_part/account_server/account_srv/
go run ./backword_part/file_server/file_srv/
go run ./other_srv/store_srv/

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
