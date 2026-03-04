# MCP API Key 认证系统 - 快速部署指南

## 📋 前置条件

- Go 1.24+ 已安装
- MySQL 数据库运行中
- clouddisk 数据库已创建

## 🚀 一键部署

### 方式 1: 使用部署脚本（推荐）

```bash
cd /home/lihaoqian/project/clouddisk_v2/backword_part/mcp_server

# 如果 MySQL 密码是 123456（默认）
./deploy_apikey.sh

# 如果 MySQL 密码不是 123456
./deploy_apikey.sh your_mysql_password
```

部署脚本会自动完成：
1. ✓ 检查数据库连接
2. ✓ 更新数据库配置
3. ✓ 运行数据库迁移
4. ✓ 备份原 main.go
5. ✓ 替换为 API Key 版本
6. ✓ 编译项目

### 方式 2: 手动部署

```bash
cd /home/lihaoqian/project/clouddisk_v2/backword_part/mcp_server

# 1. 修改 migrate.go 中的数据库密码
vim migrate.go  # 修改第 17 行的密码

# 2. 运行数据库迁移
export GOROOT=/home/lihaoqian/go
export PATH=$GOROOT/bin:$PATH
export GOPATH=/home/lihaoqian/gopath
go run migrate.go

# 3. 备份并替换 main.go
cp main.go main.go.bak
cp main_with_apikey.go main.go

# 4. 编译
go mod tidy
go build -o mcp_server .
```

## ✅ 验证部署

### 1. 启动服务

```bash
./mcp_server
```

### 2. 运行测试脚本

```bash
# 在另一个终端
./test_apikey.sh
```

测试脚本会自动执行：
- ✓ Health Check
- ✓ 生成 API Key
- ✓ 查询 API Key
- ✓ 使用 API Key 认证
- ✓ 测试无效 API Key
- ✓ 测试 Rate Limiting
- ✓ 撤销 API Key
- ✓ 验证撤销后无法使用

### 3. 手动测试

```bash
# 生成 API Key
curl -X POST http://localhost:8081/api/apikey/generate \
  -H "Content-Type: application/json" \
  -d '{"user_id": 1001, "name": "My Key", "expiry_days": 365}'

# 保存返回的 API Key
API_KEY="mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"

# 查询 API Key 信息
curl http://localhost:8081/api/apikey/get?user_id=1001

# 使用 API Key 访问 MCP 服务
curl http://localhost:8081/mcp/sse \
  -H "X-API-Key: $API_KEY"

# 查询当前用户信息
curl http://localhost:8081/api/apikey/me \
  -H "X-API-Key: $API_KEY"

# 撤销 API Key
curl -X POST http://localhost:8081/api/apikey/revoke \
  -H "Content-Type: application/json" \
  -d '{"user_id": 1001}'
```

## 📊 数据库验证

```bash
mysql -u root -p clouddisk

# 查看 API Keys
SELECT * FROM mcp_api_keys;

# 查看使用日志
SELECT * FROM mcp_api_key_logs ORDER BY request_time DESC LIMIT 10;

# 查看 Rate Limit 记录
SELECT * FROM mcp_rate_limits ORDER BY window_start DESC LIMIT 10;
```

## 🔄 回滚

如果需要回滚到旧版本：

```bash
cd /home/lihaoqian/project/clouddisk_v2/backword_part/mcp_server

# 恢复旧代码
cp main.go.bak main.go

# 重新编译
go build -o mcp_server .

# 重启服务
pkill mcp_server
./mcp_server
```

## 📝 API 接口说明

### 1. 生成 API Key

```
POST /api/apikey/generate
Content-Type: application/json

{
  "user_id": 1001,
  "name": "My API Key",
  "expiry_days": 365
}

响应:
{
  "api_key": "mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  "key_prefix": "mcp_xxxxxxxx",
  "expires_at": "2027-03-04T12:00:00Z"
}
```

**注意**: API Key 只在生成时返回一次，请妥善保存！

### 2. 查询 API Key

```
GET /api/apikey/get?user_id=1001

响应:
{
  "id": 1,
  "user_id": 1001,
  "key_prefix": "mcp_xxxxxxxx",
  "name": "My API Key",
  "status": "active",
  "expires_at": "2027-03-04T12:00:00Z",
  "last_used_at": "2026-03-04T12:00:00Z",
  "created_at": "2026-03-04T12:00:00Z"
}
```

### 3. 撤销 API Key

```
POST /api/apikey/revoke
Content-Type: application/json

{
  "user_id": 1001
}

响应:
{
  "message": "API key revoked successfully"
}
```

### 4. 获取当前用户信息（需要认证）

```
GET /api/apikey/me
X-API-Key: mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

响应:
{
  "user_id": 1001,
  "key_prefix": "mcp_xxxxxxxx",
  "name": "My API Key"
}
```

## 🔐 认证方式

API Key 可以通过以下三种方式传递：

1. **HTTP Header（推荐）**
   ```
   X-API-Key: mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
   ```

2. **Authorization Bearer**
   ```
   Authorization: Bearer mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
   ```

3. **Query 参数（不推荐）**
   ```
   http://localhost:8081/mcp/sse?api_key=mcp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
   ```

## ⚡ Rate Limiting

- 默认限制: 60 次请求/分钟
- 超过限制返回: HTTP 429 Too Many Requests
- 限流窗口: 滑动窗口（1 分钟）

## 📁 文件结构

```
backword_part/mcp_server/
├── main.go                    # 当前运行的版本
├── main.go.bak               # 原版本备份
├── main_with_apikey.go       # API Key 版本
├── migrate.go                # 数据库迁移脚本
├── deploy_apikey.sh          # 一键部署脚本
├── test_apikey.sh            # 功能测试脚本
├── apikey/
│   ├── manager.go            # API Key 管理
│   └── rate_limiter.go       # 限流器
├── middleware/
│   └── auth.go               # 认证中间件
└── handler/
    └── apikey_handler.go     # HTTP 处理器
```

## 🎯 核心特性

✅ 每个用户只能有一个活跃的 API Key
✅ 生成新 Key 自动撤销旧 Key
✅ API Key 使用 SHA256 哈希存储
✅ 支持过期时间设置
✅ 自动记录使用日志
✅ Rate Limiting（60 次/分钟）
✅ 支持手动撤销
✅ 异步日志记录（不阻塞请求）

## 🐛 故障排查

### 问题 1: 数据库连接失败

```bash
# 检查 MySQL 是否运行
systemctl status mysql

# 检查数据库是否存在
mysql -u root -p -e "SHOW DATABASES LIKE 'clouddisk';"

# 检查 migrate.go 中的密码是否正确
grep "root:" migrate.go
```

### 问题 2: 编译失败

```bash
# 清理并重新下载依赖
go clean -modcache
go mod tidy
go build -o mcp_server .
```

### 问题 3: API Key 验证失败

```bash
# 检查数据库中的 Key
mysql -u root -p clouddisk -e "SELECT * FROM mcp_api_keys WHERE status='active';"

# 查看日志
tail -f /path/to/mcp_server.log | grep -i "api\|auth"
```

## 📞 支持

详细文档请参考:
- `MCP_APIKEY_README.md` - 完整技术文档
- `MCP_APIKEY_CHECKLIST.md` - 详细检查清单

## ✨ 完成！

部署完成后，MCP 服务将要求所有请求携带有效的 API Key。用户需要先申请 API Key 才能使用 MCP 功能。
