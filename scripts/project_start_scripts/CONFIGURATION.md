# CloudDisk V2 配置说明

## 网络配置

### WSL2 环境
- WSL2 IP: `172.20.10.3`
- 所有服务通过 `127.0.0.1` 访问 K3s NodePort 服务

### K3s 基础设施服务端口

| 服务 | NodePort | 说明 |
|------|----------|------|
| Nacos | 30848 | 配置中心 (Console: 8080) |
| Consul | 30500 | 服务发现 |
| MySQL | 30306 | 数据库 |
| Redis | 31029 | 缓存 |
| Kafka | 31092 | 消息队列 |
| MinIO API | 30900 | 对象存储 |
| MinIO Console | 30901 | MinIO 管理界面 |

## 配置文件

### Go 服务配置
- **位置**: Nacos 配置中心
- **命名空间**: `ce99961c-0fcf-4f4f-81d6-ac2183f24df1`
- **Data ID**: `clouddisk.json`
- **Group**: `dev`
- **配置文件**: `/home/lihaoqian/project/clouddisk_v2/scripts/nacos-config/clouddisk.json`

Go 服务从 Nacos 动态加载配置，支持热更新。

### C++ Gateway 配置
- **位置**: 本地配置文件
- **路径**: `/home/lihaoqian/project/clouddisk_v2/forward_part/gateway/config.json`

由于 Nacos C++ SDK 与 Nacos 2.x 存在兼容性问题，Gateway 使用本地配置文件。

**重要配置项：**
```json
{
  "consul": {
    "gateway_srv": {
      "host": "172.20.10.3",  // 注册到 Consul 的地址（WSL2 IP）
      "port": 8080
    }
  }
}
```

Gateway 监听在 `0.0.0.0`（所有接口），但注册到 Consul 时使用 `172.20.10.3`，这样 Consul Pod 才能访问健康检查端点。

## 服务注册

### Consul 服务发现
- Gateway 自动注册到 Consul
- 健康检查：`http://172.20.10.3:<port>/health`
- 检查间隔：5秒
- 失败后注销：30秒

查看注册的服务：
```bash
curl http://127.0.0.1:30500/v1/agent/services | jq .
```

## 启动前检查

启动脚本会自动检查以下基础设施服务：
- ✓ Nacos (配置中心)
- ✓ Consul (服务发现)
- ✓ MySQL (数据库)
- ✓ Redis (缓存)
- ✓ Kafka (消息队列)
- ⚠ MinIO (对象存储，可选)

如果检查失败，请先启动 K3s 基础设施：
```bash
kubectl get pods -n infra
```

## 常见问题

### 1. Gateway 健康检查失败

**症状：** Consul 中 Gateway 状态为 `critical`

**原因：** Gateway 使用 `127.0.0.1` 注册，Consul Pod 无法访问

**解决：** 确保 `config.json` 中 `gateway_srv.host` 设置为 `172.20.10.3`

### 2. Go 服务无法连接 Nacos

**症状：** 服务启动失败，日志显示 Nacos 连接错误

**原因：** Nacos 未运行、配置错误，或 MySQL 中尚未初始化 `nacos_config` 库

**解决：**
```bash
# 检查 Nacos 状态
kubectl get pods -n infra | grep nacos

# 测试 Nacos 连接（Nacos 3.x）
curl http://127.0.0.1:30848/nacos/
curl http://127.0.0.1:30880/v3/console/health/liveness

# 如果日志出现 Unknown database 'nacos_config'
mysql -h127.0.0.1 -P30306 -uroot -p123456 < /home/lihaoqian/project/clouddisk_v2/scripts/sql/nacos-3.1.sql
/home/lihaoqian/project/k8s/bin/k8s-stack.sh stop nacos
/home/lihaoqian/project/k8s/bin/k8s-stack.sh start nacos
```

### 3. 服务无法注册到 Consul

**症状：** Consul 中看不到服务

**原因：** Consul 未运行、网络配置错误，或数据目录过旧导致拒绝重新加入集群

**解决：**
```bash
# 检查 Consul 状态
kubectl get pods -n infra | grep consul

# 测试 Consul 连接
curl http://127.0.0.1:30500/v1/status/leader

# 如果日志出现 server_rejoin_age_max (开发环境)
/home/lihaoqian/project/k8s/bin/k8s-stack.sh stop consul
kubectl delete pvc -n infra consul-data-consul-0
/home/lihaoqian/project/k8s/bin/k8s-stack.sh start consul
```

## 配置更新

### 更新 Nacos 配置

1. 修改配置文件：
```bash
vim /home/lihaoqian/project/clouddisk_v2/scripts/nacos-config/clouddisk.json
```

2. 导入到 Nacos：
```bash
cd /home/lihaoqian/project/clouddisk_v2/scripts
./import-nacos-config.sh
```

3. Go 服务会自动热更新配置（无需重启）

### 更新 Gateway 配置

1. 修改配置文件：
```bash
vim /home/lihaoqian/project/clouddisk_v2/forward_part/gateway/config.json
```

2. 重启 Gateway：
```bash
cd /home/lihaoqian/project/clouddisk_v2/scripts/project_start_scripts
./stop_all.sh
./start_all.sh
```

## Windows 宿主机访问

如果需要从 Windows 宿主机访问服务，需要配置端口转发（已配置）：

```powershell
# 查看端口转发
netsh interface portproxy show all

# Gateway (动态端口，需要手动添加)
netsh interface portproxy add v4tov4 listenport=<port> listenaddress=0.0.0.0 connectport=<port> connectaddress=172.20.10.3
```

## 监控和调试

### 查看服务状态
```bash
cd /home/lihaoqian/project/clouddisk_v2/scripts/project_start_scripts
./status.sh
```

### 查看日志
```bash
# 实时查看所有日志
tail -f /home/lihaoqian/project/clouddisk_v2/scripts/project_start_scripts/log/*.log

# 查看特定服务日志
tail -f /home/lihaoqian/project/clouddisk_v2/scripts/project_start_scripts/log/gateway.log
```

### Consul UI
```
http://127.0.0.1:30500/ui
```

### Nacos Console
```
http://127.0.0.1:30848/nacos
用户名: nacos
密码: nacos
```
