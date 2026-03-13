# Nacos 配置说明文档

## 一、配置文件说明

### 配置文件位置
```
/home/lihaoqian/project/clouddisk_v2/scripts/nacos-config/clouddisk.json
```

### 配置结构

```json
{
  "redis": {
    "host": "172.20.10.3",      // Redis 主机地址
    "port": "31029"              // Redis 端口
  },
  "mysql": {
    "host": "172.20.10.3",      // MySQL 主机地址
    "port": "30306",             // MySQL 端口
    "user": "root",              // MySQL 用户名
    "password": "123456"         // MySQL 密码
  },
  "consul": {
    "host": "172.20.10.3",      // Consul 主机地址
    "port": "30500",             // Consul 端口
    "account_srv": {
      "host": "0.0.0.0",        // account_srv 监听地址
      "port": 50051             // account_srv gRPC 端口
    },
    "file_srv": {
      "host": "0.0.0.0",        // file_srv 监听地址
      "port": 50052             // file_srv gRPC 端口
    },
    "mcp_srv": {
      "host": "0.0.0.0",        // mcp_srv 监听地址
      "port": 50053             // mcp_srv gRPC 端口
    }
  },
  "jwt": {
    "signing_key": "clouddisk_v2_secret_key_2025"  // JWT 签名密钥
  },
  "kafka": {
    "host": "172.20.10.3",      // Kafka 主机地址
    "port": "31092"              // Kafka 端口
  },
  "alioss": {
    "endpoint": "oss-cn-hangzhou.aliyuncs.com",    // 阿里云 OSS Endpoint
    "access_key_id": "your_access_key_id",         // 阿里云 AccessKey ID
    "access_key_secret": "your_access_key_secret", // 阿里云 AccessKey Secret
    "bucket_name": "clouddisk-v2"                  // OSS Bucket 名称
  },
  "minio": {
    "host": "172.20.10.3",      // MinIO 主机地址
    "port": 30900,               // MinIO S3 API 端口
    "accessKey": "minioadmin",   // MinIO AccessKey
    "secretKey": "minioadmin12345678",  // MinIO SecretKey
    "bucket_name": "clouddisk"   // MinIO Bucket 名称
  }
}
```

## 二、配置项详解

### 1. Redis 配置
```json
"redis": {
  "host": "172.20.10.3",
  "port": "31029"
}
```
- **用途**: 缓存、会话存储
- **测试连接**: `redis-cli -h 172.20.10.3 -p 31029 ping`

### 2. MySQL 配置
```json
"mysql": {
  "host": "172.20.10.3",
  "port": "30306",
  "user": "root",
  "password": "123456"
}
```
- **用途**: 主数据库
- **数据库名**: `orm_test` (自动创建)
- **测试连接**: `mysql -h 172.20.10.3 -P 30306 -uroot -p123456`

### 3. Consul 配置
```json
"consul": {
  "host": "172.20.10.3",
  "port": "30500",
  "account_srv": {"host": "0.0.0.0", "port": 50051},
  "file_srv": {"host": "0.0.0.0", "port": 50052},
  "mcp_srv": {"host": "0.0.0.0", "port": 50053}
}
```
- **用途**: 服务注册与发现
- **服务端口分配**:
  - account_srv: 50051 (gRPC)
  - file_srv: 50052 (gRPC)
  - mcp_srv: 50053 (gRPC)
- **Consul UI**: http://172.20.10.3:30500

### 4. JWT 配置
```json
"jwt": {
  "signing_key": "clouddisk_v2_secret_key_2025"
}
```
- **用途**: 用户认证 Token 签名
- **建议**: 生产环境使用更复杂的密钥

### 5. Kafka 配置
```json
"kafka": {
  "host": "172.20.10.3",
  "port": "31092"
}
```
- **用途**: 异步消息队列（文件上传事件）
- **Topic**: `alioss`
- **测试**: 见下文 Kafka 测试部分

### 6. 阿里云 OSS 配置
```json
"alioss": {
  "endpoint": "oss-cn-hangzhou.aliyuncs.com",
  "access_key_id": "your_access_key_id",
  "access_key_secret": "your_access_key_secret",
  "bucket_name": "clouddisk-v2"
}
```
- **用途**: 云端对象存储（可选）
- **注意**: 需要替换为实际的阿里云凭证

### 7. MinIO 配置
```json
"minio": {
  "host": "172.20.10.3",
  "port": 30900,
  "accessKey": "minioadmin",
  "secretKey": "minioadmin12345678",
  "bucket_name": "clouddisk"
}
```
- **用途**: 本地对象存储
- **MinIO Console**: http://172.20.10.3:30901
- **需要手动创建 Bucket**: `clouddisk`

## 三、导入配置到 Nacos

### 方法一：使用导入脚本（推荐）

```bash
cd /home/lihaoqian/project/clouddisk_v2/scripts

# 执行导入脚本
./import-nacos-config.sh
```

### 方法二：手动导入（通过 Nacos Console）

1. **访问 Nacos Console**
   ```
   http://172.20.10.3:30848/nacos
   用户名: nacos
   密码: nacos
   ```

2. **创建命名空间**
   - 进入 "命名空间" 页面
   - 点击 "新建命名空间"
   - 命名空间 ID: `ce99961c-0fcf-4f4f-81d6-ac2183f24df1`
   - 命名空间名: `clouddisk-dev`
   - 点击 "确定"

3. **导入配置**
   - 切换到新创建的命名空间
   - 进入 "配置管理" → "配置列表"
   - 点击 "+" 新建配置
   - Data ID: `clouddisk.json`
   - Group: `dev`
   - 配置格式: `JSON`
   - 配置内容: 复制 `clouddisk.json` 的内容
   - 点击 "发布"

### 方法三：使用 Nacos Open API

```bash
# 获取本地 IP
LOCAL_IP=$(hostname -I | awk '{print $1}')

# 导入配置
curl -X POST "http://${LOCAL_IP}:30848/nacos/v1/cs/configs" \
  -d "dataId=clouddisk.json" \
  -d "group=dev" \
  -d "tenant=ce99961c-0fcf-4f4f-81d6-ac2183f24df1" \
  -d "content=$(cat nacos-config/clouddisk.json)" \
  -d "type=json" \
  --user "nacos:nacos"
```

## 四、修改服务代码中的 Nacos 配置

### 修改 viper_config_centre.go

```go
// 文件位置: /home/lihaoqian/project/clouddisk_v2/internal/viper_config_centre.go

// 修改 Nacos 服务配置
serverConfigs := []constant.ServerConfig{
    {
        IpAddr: "172.20.10.3",  // 修改为实际 IP
        Port:   30848,
        Scheme: "http",
    },
}

// 修改命名空间 ID
clientConfig := constant.ClientConfig{
    NamespaceId: "ce99961c-0fcf-4f4f-81d6-ac2183f24df1",  // 确保与 Nacos 中创建的一致
    TimeoutMs:   10000,
    Username:    "nacos",
    Password:    "nacos",
    // ... 其他配置
}
```

## 五、验证配置

### 1. 在 Nacos Console 中验证

访问: http://172.20.10.3:30848/nacos

- 切换到命名空间: `ce99961c-0fcf-4f4f-81d6-ac2183f24df1`
- 查看配置列表
- 确认 `clouddisk.json` 存在且内容正确

### 2. 测试各个服务连接

```bash
# 测试 Redis
redis-cli -h 172.20.10.3 -p 31029 ping

# 测试 MySQL
mysql -h 172.20.10.3 -P 30306 -uroot -p123456 -e "SHOW DATABASES;"

# 测试 Consul
curl http://172.20.10.3:30500/v1/status/leader

# 测试 Kafka
# (需要进入 Kafka Pod)
kubectl exec -it -n infra $(kubectl get pod -n infra -l app=kafka -o jsonpath='{.items[0].metadata.name}') -- \
  kafka-topics.sh --list --bootstrap-server localhost:9092
```

### 3. 创建 MinIO Bucket

```bash
# 方法一：通过 MinIO Console
# 访问: http://172.20.10.3:30901
# 登录后创建名为 "clouddisk" 的 Bucket

# 方法二：使用 mc 命令行工具
mc alias set myminio http://172.20.10.3:30900 minioadmin minioadmin12345678
mc mb myminio/clouddisk
mc ls myminio
```

## 六、配置热更新

Nacos 支持配置热更新，修改配置后服务会自动重新加载。

### 测试热更新

1. 在 Nacos Console 中修改配置
2. 观察服务日志，应该看到类似信息：
   ```
   nacos config changed
   ```
3. 配置会自动重新加载，无需重启服务

## 七、环境变量配置（可选）

如果不想硬编码 IP 地址，可以使用环境变量：

```bash
# 设置环境变量
export NACOS_SERVER_ADDR="172.20.10.3:30848"
export NACOS_NAMESPACE_ID="ce99961c-0fcf-4f4f-81d6-ac2183f24df1"
export NACOS_USERNAME="nacos"
export NACOS_PASSWORD="nacos"
```

然后在代码中读取环境变量。

## 八、多环境配置

### 开发环境 (dev)
- Namespace: `ce99961c-0fcf-4f4f-81d6-ac2183f24df1`
- Group: `dev`
- Data ID: `clouddisk.json`

### 测试环境 (test)
- Namespace: 创建新的 namespace
- Group: `test`
- Data ID: `clouddisk.json`

### 生产环境 (prod)
- Namespace: 创建新的 namespace
- Group: `prod`
- Data ID: `clouddisk.json`

## 九、故障排查

### 问题 1: 服务无法连接 Nacos

**检查步骤:**
```bash
# 1. 检查 Nacos 是否运行
kubectl get pods -n infra | grep nacos

# 2. 检查 Nacos 日志
kubectl logs -n infra -l app=nacos --tail=50

# 3. 测试 Nacos API
curl http://172.20.10.3:30848/nacos/v1/console/health/readiness
```

### 问题 2: 配置未生效

**检查步骤:**
1. 确认 Namespace ID 正确
2. 确认 Data ID 和 Group 正确
3. 检查配置格式是否为有效 JSON
4. 查看服务日志中的 Nacos 连接信息

### 问题 3: 服务无法连接 MySQL/Redis 等

**检查步骤:**
```bash
# 检查服务状态
./manage-infra.sh status

# 测试连接
mysql -h 172.20.10.3 -P 30306 -uroot -p123456
redis-cli -h 172.20.10.3 -p 31029 ping
```

## 十、配置模板

### 生产环境配置模板

```json
{
  "redis": {
    "host": "redis-prod.example.com",
    "port": "6379"
  },
  "mysql": {
    "host": "mysql-prod.example.com",
    "port": "3306",
    "user": "clouddisk",
    "password": "strong_password_here"
  },
  "consul": {
    "host": "consul-prod.example.com",
    "port": "8500",
    "account_srv": {"host": "0.0.0.0", "port": 50051},
    "file_srv": {"host": "0.0.0.0", "port": 50052},
    "mcp_srv": {"host": "0.0.0.0", "port": 50053}
  },
  "jwt": {
    "signing_key": "use_a_very_strong_random_key_here"
  },
  "kafka": {
    "host": "kafka-prod.example.com",
    "port": "9092"
  },
  "alioss": {
    "endpoint": "oss-cn-hangzhou.aliyuncs.com",
    "access_key_id": "LTAI...",
    "access_key_secret": "...",
    "bucket_name": "clouddisk-prod"
  },
  "minio": {
    "host": "minio-prod.example.com",
    "port": 9000,
    "accessKey": "prod_access_key",
    "secretKey": "prod_secret_key",
    "bucket_name": "clouddisk-prod"
  }
}
```

## 十一、安全建议

1. **修改默认密码**
   - Nacos 默认密码: nacos/nacos
   - MySQL 默认密码: root/123456
   - MinIO 默认密码: minioadmin/minioadmin12345678

2. **使用强密钥**
   - JWT signing_key 使用随机生成的强密钥
   - 生产环境不要使用示例中的密钥

3. **网络隔离**
   - 生产环境使用内网地址
   - 配置防火墙规则
   - 使用 TLS/SSL 加密通信

4. **权限控制**
   - Nacos 配置权限管理
   - MySQL 使用专用用户，不要使用 root
   - MinIO 配置访问策略

---

**配置完成后，即可启动服务进行测试！**
