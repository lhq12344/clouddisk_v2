#!/bin/bash

# CloudDisk V2 一键启动脚本
# 启动所有服务：OpenResty/Nginx、C++ Gateway、Go 微服务、React 前端

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="/home/lihaoqian/project/clouddisk_v2"
cd "$PROJECT_ROOT"

# 日志目录
LOG_DIR="$PROJECT_ROOT/log"
mkdir -p "$LOG_DIR"

# PID 文件目录
PID_DIR="$PROJECT_ROOT/.pids"
mkdir -p "$PID_DIR"

# Go 构建缓存目录
GO_CACHE_DIR="$PROJECT_ROOT/.cache/go-build"
mkdir -p "$GO_CACHE_DIR"

K8S_NAMESPACE="infra"
K8S_SCRIPT="/home/lihaoqian/project/k8s/bin/k8s-stack.sh"
NACOS_SQL="$PROJECT_ROOT/scripts/sql/nacos-3.1.sql"
NGINX_MODE_FILE="$PID_DIR/nginx.mode"
DOCKER_OPENRESTY_IMAGE="${DOCKER_OPENRESTY_IMAGE:-swr.cn-north-4.myhuaweicloud.com/ddn-k8s/docker.io/uusec/openresty-manager:latest}"
DOCKER_OPENRESTY_CONTAINER="${DOCKER_OPENRESTY_CONTAINER:-clouddisk_v2_openresty}"
CLAMD_HELPER="$PROJECT_ROOT/scripts/project_start_scripts/clamd_local.sh"
K8S_RUNTIME_RECONCILER="$PROJECT_ROOT/scripts/project_start_scripts/reconcile_k8s_runtime.sh"
CORE_RUNTIME_PROFILE="${CORE_RUNTIME_PROFILE:-core}"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CloudDisk V2 一键启动脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

http_health_ok() {
    local url="$1"
    no_proxy="*" curl -fsS -m 3 "$url" > /dev/null 2>&1
}

http_status_ok() {
    local url="$1"
    local expected_regex="$2"
    local status_code

    status_code=$(no_proxy="*" curl -sS -m 3 -o /dev/null -w '%{http_code}' "$url" 2>/dev/null || true)
    [[ "$status_code" =~ $expected_regex ]]
}

tcp_port_open() {
    local host="$1"
    local port="$2"
    timeout 2 bash -c "cat < /dev/null > /dev/tcp/${host}/${port}" 2>/dev/null
}

service_pid_running() {
    local service_name="$1"
    local pid_file="$PID_DIR/${service_name}.pid"
    local pid=""

    if [ ! -f "$pid_file" ]; then
        return 1
    fi

    pid=$(cat "$pid_file" 2>/dev/null || true)
    if [ -z "$pid" ]; then
        return 1
    fi

    ps -p "$pid" > /dev/null 2>&1
}

port_listening() {
    local port="$1"
    if command -v ss > /dev/null 2>&1; then
        ss -ltn 2>/dev/null | grep -q ":${port}\\b"
        return
    fi
    netstat -ltn 2>/dev/null | grep -q ":${port} "
}

listening_pid_for_port() {
    local port="$1"

    if command -v lsof > /dev/null 2>&1; then
        lsof -tiTCP:"$port" -sTCP:LISTEN 2>/dev/null | head -1
        return
    fi

    if command -v ss > /dev/null 2>&1; then
        ss -ltnp 2>/dev/null | awk -v port=":${port}" '$4 ~ port { if (match($NF, /pid=([0-9]+)/, m)) { print m[1]; exit } }'
    fi
}

runtime_services() {
    echo "gateway outbox_relay storage_control store_srv ai_srv mcp_srv frontend"
}

configure_core_feature_flags() {
    case "$CORE_RUNTIME_PROFILE" in
        legacy)
            : "${CORE_ACCOUNT_READS:=legacy}"
            : "${CORE_ACCOUNT_LOGIN:=legacy}"
            : "${CORE_ACCOUNT_REGISTRATION:=legacy}"
            : "${CORE_FILE_READS:=legacy}"
            : "${CORE_FILE_ACCESS:=legacy}"
            : "${CORE_UPLOAD_CONTROL:=legacy}"
            : "${CORE_UPLOAD_COMPLETE:=legacy}"
            : "${CORE_FILE_DELETE:=legacy}"
            : "${CORE_SMALL_UPLOAD_DIRECT:=legacy}"
            : "${CORE_OUTBOX_RELAY:=legacy}"
            ;;
        mixed)
            : "${CORE_ACCOUNT_READS:=shadow}"
            : "${CORE_ACCOUNT_LOGIN:=core}"
            : "${CORE_ACCOUNT_REGISTRATION:=core}"
            : "${CORE_FILE_READS:=shadow}"
            : "${CORE_FILE_ACCESS:=shadow}"
            : "${CORE_UPLOAD_CONTROL:=shadow}"
            : "${CORE_UPLOAD_COMPLETE:=core}"
            : "${CORE_FILE_DELETE:=core}"
            : "${CORE_SMALL_UPLOAD_DIRECT:=core}"
            : "${CORE_OUTBOX_RELAY:=core}"
            ;;
        core|*)
            : "${CORE_ACCOUNT_READS:=core}"
            : "${CORE_ACCOUNT_LOGIN:=core}"
            : "${CORE_ACCOUNT_REGISTRATION:=core}"
            : "${CORE_FILE_READS:=core}"
            : "${CORE_FILE_ACCESS:=core}"
            : "${CORE_UPLOAD_CONTROL:=core}"
            : "${CORE_UPLOAD_COMPLETE:=core}"
            : "${CORE_FILE_DELETE:=core}"
            : "${CORE_SMALL_UPLOAD_DIRECT:=core}"
            : "${CORE_OUTBOX_RELAY:=core}"
            ;;
    esac

    export CORE_ACCOUNT_READS CORE_ACCOUNT_LOGIN CORE_ACCOUNT_REGISTRATION
    export CORE_FILE_READS CORE_FILE_ACCESS CORE_UPLOAD_CONTROL CORE_UPLOAD_COMPLETE
    export CORE_FILE_DELETE CORE_SMALL_UPLOAD_DIRECT CORE_OUTBOX_RELAY
}

k8s_openresty_running() {
    if ! k8s_available; then
        return 1
    fi

    kubectl get pods -n "$K8S_NAMESPACE" -l app=openresty --no-headers 2>/dev/null \
        | awk '{split($2, ready, "/"); if (ready[1] == ready[2] && $3 == "Running") found=1} END {exit found ? 0 : 1}'
}

text_matches() {
    local pattern="$1"
    if command -v rg > /dev/null 2>&1; then
        rg -q "$pattern"
    else
        grep -Eq "$pattern"
    fi
}

nacos_health_ok() {
    http_health_ok "http://127.0.0.1:30848/nacos/" || http_status_ok "http://127.0.0.1:30880/v3/console/health/liveness" '^(200)$'
}

k8s_available() {
    command -v kubectl &> /dev/null && kubectl get namespace "$K8S_NAMESPACE" > /dev/null 2>&1
}

reconcile_k8s_runtime() {
    if ! k8s_available; then
        return
    fi

    if [ ! -x "$K8S_RUNTIME_RECONCILER" ]; then
        return
    fi

    echo -e "${YELLOW}[预处理] 校正 K8s 运行态与外部清单...${NC}"
    "$K8S_RUNTIME_RECONCILER" || echo -e "${YELLOW}  ⚠ K8s 自愈步骤未完全成功，继续现有启动流程${NC}"
    echo ""
}

print_k8s_pod_status() {
    local app_name="$1"
    local pod_table

    pod_table=$(kubectl get pods -n "$K8S_NAMESPACE" -l "app=${app_name}" --no-headers 2>/dev/null || true)
    if [ -n "$pod_table" ]; then
        echo -e "${YELLOW}    K8s Pod 状态:${NC}"
        echo "$pod_table" | sed 's/^/      /'
    else
        echo -e "${YELLOW}    K8s: 未发现 app=${app_name} 的 Pod${NC}"
    fi
}

print_k8s_service_status() {
    local service_name="$1"
    local service_table endpoints

    service_table=$(kubectl get svc -n "$K8S_NAMESPACE" "$service_name" --no-headers 2>/dev/null || true)
    if [ -n "$service_table" ]; then
        echo -e "${YELLOW}    K8s Service:${NC}"
        echo "$service_table" | sed 's/^/      /'
    fi

    endpoints=$(kubectl get endpoints -n "$K8S_NAMESPACE" "$service_name" -o jsonpath='{range .subsets[*]}{range .addresses[*]}{.ip}{":"}{range $.subsets[*].ports[*]}{.port}{" "}{end}{end}{end}' 2>/dev/null || true)
    if [ -n "$endpoints" ]; then
        echo -e "${YELLOW}    Endpoints: ${endpoints}${NC}"
    else
        echo -e "${YELLOW}    Endpoints: 无可用后端${NC}"
    fi
}

diagnose_nacos() {
    if ! k8s_available; then
        return
    fi

    print_k8s_pod_status "nacos"
    print_k8s_service_status "nacos"

    local nacos_logs
    nacos_logs=$(kubectl logs -n "$K8S_NAMESPACE" deployment/nacos --tail=80 2>/dev/null || true)
    if echo "$nacos_logs" | text_matches "Unknown database 'nacos_config'|db-load-error"; then
        echo -e "${RED}    根因: Nacos MySQL 库未初始化，缺少数据库 nacos_config${NC}"
        echo -e "${YELLOW}    修复步骤:${NC}"
        echo -e "      mysql -h127.0.0.1 -P30306 -uroot -p123456 < $NACOS_SQL"
        echo -e "      $K8S_SCRIPT stop nacos"
        echo -e "      $K8S_SCRIPT start nacos"
        return
    fi

    echo -e "${YELLOW}    排查命令:${NC}"
    echo -e "      kubectl logs -n $K8S_NAMESPACE deployment/nacos --tail=80"
    echo -e "      kubectl describe pod -n $K8S_NAMESPACE -l app=nacos"
}

diagnose_consul() {
    if ! k8s_available; then
        return
    fi

    print_k8s_pod_status "consul"
    print_k8s_service_status "consul-ui"

    local consul_logs
    consul_logs=$(kubectl logs -n "$K8S_NAMESPACE" statefulset/consul --tail=80 2>/dev/null || true)
    if echo "$consul_logs" | text_matches "server_rejoin_age_max"; then
        echo -e "${RED}    根因: Consul 数据目录过旧，超过 server_rejoin_age_max，拒绝重新加入${NC}"
        echo -e "${YELLOW}    修复步骤(开发环境):${NC}"
        echo -e "      $K8S_SCRIPT stop consul"
        echo -e "      kubectl delete pvc -n $K8S_NAMESPACE consul-data-consul-0"
        echo -e "      $K8S_SCRIPT start consul"
        return
    fi

    echo -e "${YELLOW}    排查命令:${NC}"
    echo -e "      kubectl logs -n $K8S_NAMESPACE statefulset/consul --tail=80"
    echo -e "      kubectl get svc,endpoints -n $K8S_NAMESPACE consul-ui"
}

# 检查基础设施服务
check_infrastructure() {
    echo -e "${YELLOW}[0/7] 检查基础设施服务...${NC}"

    INFRA_OK=true

    # 检查 Nacos
    if nacos_health_ok; then
        echo -e "${GREEN}  ✓ Nacos (127.0.0.1:30848)${NC}"
    else
        echo -e "${RED}  ✗ Nacos 不可用 (127.0.0.1:30848)${NC}"
        diagnose_nacos
        INFRA_OK=false
    fi

    # 检查 Consul
    if http_health_ok "http://127.0.0.1:30500/v1/status/leader"; then
        echo -e "${GREEN}  ✓ Consul (127.0.0.1:30500)${NC}"
    else
        echo -e "${RED}  ✗ Consul 不可用 (127.0.0.1:30500)${NC}"
        diagnose_consul
        INFRA_OK=false
    fi

    # 检查 MySQL
    if tcp_port_open "127.0.0.1" "30306"; then
        echo -e "${GREEN}  ✓ MySQL (127.0.0.1:30306)${NC}"
    else
        echo -e "${RED}  ✗ MySQL 未运行 (127.0.0.1:30306)${NC}"
        INFRA_OK=false
    fi

    # 检查 Redis
    if tcp_port_open "127.0.0.1" "31029"; then
        echo -e "${GREEN}  ✓ Redis (127.0.0.1:31029)${NC}"
    else
        echo -e "${RED}  ✗ Redis 未运行 (127.0.0.1:31029)${NC}"
        INFRA_OK=false
    fi

    # 检查 Kafka
    if tcp_port_open "127.0.0.1" "31092"; then
        echo -e "${GREEN}  ✓ Kafka (127.0.0.1:31092)${NC}"
    else
        echo -e "${RED}  ✗ Kafka 未运行 (127.0.0.1:31092)${NC}"
        INFRA_OK=false
    fi

    # 检查 MinIO
    if http_health_ok "http://127.0.0.1:30900/minio/health/live"; then
        echo -e "${GREEN}  ✓ MinIO (127.0.0.1:30900)${NC}"
    else
        echo -e "${YELLOW}  ⚠ MinIO 未运行 (127.0.0.1:30900)${NC}"
    fi

    if [ "$INFRA_OK" = false ]; then
        echo ""
        echo -e "${RED}错误: 部分基础设施服务不可用！${NC}"
        echo -e "${YELLOW}如果组件尚未部署，可执行：${NC}"
        echo -e "  $K8S_SCRIPT deploy mysql"
        echo -e "  $K8S_SCRIPT deploy redis"
        echo -e "  $K8S_SCRIPT deploy minio"
        echo -e "  $K8S_SCRIPT deploy consul"
        echo -e "  $K8S_SCRIPT deploy zookeeper"
        echo -e "  $K8S_SCRIPT deploy kafka"
        echo -e "  $K8S_SCRIPT deploy nacos"
        echo -e "  $K8S_SCRIPT status clouddisk_v2"
        echo ""
        exit 1
    fi

    echo ""
}

# 检查依赖
check_dependencies() {
    echo -e "${YELLOW}[1/7] 检查依赖...${NC}"

    # 检查 Go
    if ! command -v go &> /dev/null; then
        echo -e "${RED}错误: 未找到 Go，请先安装 Go${NC}"
        exit 1
    fi
    echo -e "${GREEN}  ✓ Go: $(go version)${NC}"

    # 检查 OpenResty 或 Nginx
    NGINX_BIN=""
    NGINX_RUNTIME=""
    if command -v openresty &> /dev/null; then
        NGINX_BIN="openresty"
        NGINX_RUNTIME="local"
        echo -e "${GREEN}  ✓ OpenResty: $(openresty -v 2>&1 | head -1)${NC}"
    elif command -v nginx &> /dev/null; then
        NGINX_BIN="nginx"
        NGINX_RUNTIME="local"
        echo -e "${GREEN}  ✓ Nginx: $(nginx -v 2>&1)${NC}"
    elif k8s_openresty_running; then
        NGINX_RUNTIME="k8s"
        echo -e "${GREEN}  ✓ OpenResty/Nginx 已由 K8s 接管${NC}"
        echo -e "${GREEN}    Pod: $(kubectl get pods -n "$K8S_NAMESPACE" -l app=openresty -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)${NC}"
    elif command -v docker &> /dev/null; then
        NGINX_RUNTIME="docker"
        echo -e "${YELLOW}  ⚠ 未找到本机 OpenResty/Nginx，将使用 Docker 镜像启动反向代理${NC}"
        echo -e "${GREEN}    镜像: ${DOCKER_OPENRESTY_IMAGE}${NC}"
    else
        echo -e "${YELLOW}  ⚠ 未找到 OpenResty/Nginx，将跳过反向代理启动${NC}"
    fi

    # 检查 C++ Gateway 二进制
    if [ -f "$PROJECT_ROOT/forward_part/gateway/build/gateway" ]; then
        echo -e "${GREEN}  ✓ C++ Gateway 已编译${NC}"
    else
        echo -e "${YELLOW}  ⚠ C++ Gateway 未编译，将尝试编译...${NC}"
        build_cpp_gateway
    fi

    # 检查 Node.js (前端)
    if command -v npm &> /dev/null; then
        echo -e "${GREEN}  ✓ Node.js: $(node -v)${NC}"
    else
        echo -e "${YELLOW}  ⚠ 未找到 Node.js，将跳过前端启动${NC}"
    fi

    if command -v clamd &> /dev/null; then
        echo -e "${GREEN}  ✓ ClamAV: $(clamd --version | head -1)${NC}"
    else
        echo -e "${YELLOW}  ⚠ 未找到 clamd，大文件病毒扫描将不可用${NC}"
    fi

    echo ""
}

# 编译 C++ Gateway
build_cpp_gateway() {
    echo -e "${YELLOW}[编译] 编译 C++ Gateway...${NC}"
    cd "$PROJECT_ROOT/forward_part/gateway"

    if [ ! -d "build" ]; then
        mkdir -p build
    fi

    cd build
    cmake .. && make -j$(nproc)

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}  ✓ C++ Gateway 编译成功${NC}"
    else
        echo -e "${RED}  ✗ C++ Gateway 编译失败${NC}"
        exit 1
    fi

    cd "$PROJECT_ROOT"
    echo ""
}

# 启动 OpenResty/Nginx
start_nginx() {
    if [ -z "${NGINX_RUNTIME:-}" ] || [ "$NGINX_RUNTIME" = "k8s" ]; then
        echo -e "${YELLOW}[2/7] 跳过 OpenResty/Nginx 启动${NC}"
        if [ "${NGINX_RUNTIME:-}" = "k8s" ]; then
            echo -e "${GREEN}  ✓ K8s OpenResty 已在运行，继续使用现有实例${NC}"
        fi
        echo ""
        return
    fi

    echo -e "${YELLOW}[2/7] 启动 OpenResty/Nginx...${NC}"

    NGINX_CONF="$PROJECT_ROOT/forward_part/config/nginx/nginx.conf"

    if [ ! -f "$NGINX_CONF" ]; then
        echo -e "${RED}  ✗ 未找到 nginx.conf: $NGINX_CONF${NC}"
        echo ""
        return
    fi

    # 更新配置文件中的路径（如果需要）
    sed -i "s|/home/lihaoqian/myproject/clouddisk_v2|$PROJECT_ROOT|g" "$NGINX_CONF"

    rm -f "$NGINX_MODE_FILE"

    if [ "$NGINX_RUNTIME" = "docker" ]; then
        docker image inspect "$DOCKER_OPENRESTY_IMAGE" > /dev/null 2>&1 || docker pull "$DOCKER_OPENRESTY_IMAGE"
        docker rm -f "$DOCKER_OPENRESTY_CONTAINER" > /dev/null 2>&1 || true

        docker run -d \
            --name "$DOCKER_OPENRESTY_CONTAINER" \
            --network host \
            -v "$PROJECT_ROOT:$PROJECT_ROOT" \
            --entrypoint openresty \
            "$DOCKER_OPENRESTY_IMAGE" \
            -c "$NGINX_CONF" \
            -p "$PROJECT_ROOT/forward_part/config/nginx/" \
            -g "daemon off;" > /dev/null

        sleep 2

        if [ "$(docker inspect -f '{{.State.Running}}' "$DOCKER_OPENRESTY_CONTAINER" 2>/dev/null || true)" = "true" ]; then
            echo "docker" > "$NGINX_MODE_FILE"
            echo -e "${GREEN}  ✓ OpenResty/Nginx 已通过 Docker 启动${NC}"
            echo -e "${GREEN}    容器: $DOCKER_OPENRESTY_CONTAINER${NC}"
        else
            echo -e "${RED}  ✗ OpenResty/Nginx Docker 启动失败${NC}"
            docker logs --tail 40 "$DOCKER_OPENRESTY_CONTAINER" 2>&1 | sed 's/^/    /' || true
        fi
    else
        # 启动本机 Nginx/OpenResty
        $NGINX_BIN -c "$NGINX_CONF" -p "$PROJECT_ROOT/forward_part/config/nginx/"
        if [ $? -eq 0 ]; then
            echo "local" > "$NGINX_MODE_FILE"
            echo -e "${GREEN}  ✓ OpenResty/Nginx 已启动${NC}"
        else
            echo -e "${RED}  ✗ OpenResty/Nginx 启动失败${NC}"
        fi
    fi

    echo ""
}

# 启动 C++ Gateway
start_cpp_gateway() {
    echo -e "${YELLOW}[3/7] 启动 C++ Gateway...${NC}"

    if service_pid_running "gateway"; then
        echo -e "${GREEN}  ✓ C++ Gateway 已在运行，跳过重复启动${NC}"
        echo ""
        return
    fi

    GATEWAY_BIN="$PROJECT_ROOT/forward_part/gateway/build/gateway"
    GATEWAY_CONFIG="$PROJECT_ROOT/forward_part/gateway/config.json"

    if [ ! -f "$GATEWAY_BIN" ]; then
        echo -e "${RED}  ✗ 未找到 Gateway 二进制文件${NC}"
        echo ""
        return
    fi

    if [ ! -f "$GATEWAY_CONFIG" ]; then
        echo -e "${RED}  ✗ 未找到 Gateway 配置文件: $GATEWAY_CONFIG${NC}"
        echo ""
        return
    fi

    cd "$PROJECT_ROOT/forward_part/gateway"

    nohup ./build/gateway > "$LOG_DIR/gateway.log" 2>&1 &
    GATEWAY_PID=$!
    echo $GATEWAY_PID > "$PID_DIR/gateway.pid"

    sleep 2

    if ps -p $GATEWAY_PID > /dev/null; then
        echo -e "${GREEN}  ✓ C++ Gateway 已启动 (PID: $GATEWAY_PID)${NC}"
        # 获取实际监听的端口
        GATEWAY_PORT=$(lsof -i -P -n | grep $GATEWAY_PID | grep LISTEN | awk '{print $9}' | cut -d: -f2 | head -1)
        if [ ! -z "$GATEWAY_PORT" ]; then
            echo -e "${GREEN}    监听端口: $GATEWAY_PORT${NC}"
        fi
    else
        echo -e "${RED}  ✗ C++ Gateway 启动失败，查看日志: $LOG_DIR/gateway.log${NC}"
    fi

    cd "$PROJECT_ROOT"
    echo ""
}

# 启动 Go 微服务
start_go_services() {
    echo -e "${YELLOW}[4/7] 启动 Go 微服务...${NC}"
    echo -e "  运行拓扑: ${CORE_RUNTIME_PROFILE}"

    if [ -x "$CLAMD_HELPER" ] && command -v clamd > /dev/null 2>&1; then
        echo -e "  启动 clamd_local..."
        "$CLAMD_HELPER" start | sed 's/^/    /'
    fi

    echo -e "${YELLOW}  ○ Core profile: legacy account/file services are retired${NC}"

    # Outbox Relay Service
    if service_pid_running "outbox_relay"; then
        echo -e "${GREEN}  ✓ outbox_relay 已在运行，跳过重复启动${NC}"
    else
        echo -e "  启动 outbox_relay..."
        nohup env GOCACHE="$GO_CACHE_DIR" go run ./backword_part/outbox_relay/ > "$LOG_DIR/outbox_relay.log" 2>&1 &
        OUTBOX_RELAY_PID=$!
        echo $OUTBOX_RELAY_PID > "$PID_DIR/outbox_relay.pid"
        echo -e "${GREEN}    ✓ outbox_relay (PID: $OUTBOX_RELAY_PID)${NC}"
    fi

    sleep 2

    # Storage Control Service
    if service_pid_running "storage_control"; then
        echo -e "${GREEN}  ✓ storage_control 已在运行，跳过重复启动${NC}"
    else
        echo -e "  启动 storage_control..."
        nohup env GOCACHE="$GO_CACHE_DIR" go run ./backword_part/storage_control/ > "$LOG_DIR/storage_control.log" 2>&1 &
        STORAGE_CONTROL_PID=$!
        echo $STORAGE_CONTROL_PID > "$PID_DIR/storage_control.pid"
        echo -e "${GREEN}    ✓ storage_control (PID: $STORAGE_CONTROL_PID)${NC}"
    fi

    sleep 2

    # Store Service
    if service_pid_running "store_srv"; then
        echo -e "${GREEN}  ✓ store_srv 已在运行，跳过重复启动${NC}"
    else
        echo -e "  启动 store_srv..."
        nohup env GOCACHE="$GO_CACHE_DIR" go run ./other_srv/store_srv/ > "$LOG_DIR/store_srv.log" 2>&1 &
        STORE_PID=$!
        echo $STORE_PID > "$PID_DIR/store_srv.pid"
        echo -e "${GREEN}    ✓ store_srv (PID: $STORE_PID)${NC}"
    fi

    sleep 2

    # AI Service (可选)
    if [ -f "./backword_part/AI_server/main.go" ]; then
        if service_pid_running "ai_srv"; then
            echo -e "${GREEN}  ✓ AI_srv 已在运行，跳过重复启动${NC}"
        else
            echo -e "  启动 AI_srv..."
            nohup env GOCACHE="$GO_CACHE_DIR" go run ./backword_part/AI_server/ > "$LOG_DIR/ai_srv.log" 2>&1 &
            AI_PID=$!
            echo $AI_PID > "$PID_DIR/ai_srv.pid"
            echo -e "${GREEN}    ✓ AI_srv (PID: $AI_PID)${NC}"
        fi
        sleep 1
    fi

    # MCP Service (可选)
    if [ -f "./backword_part/mcp_server/main.go" ]; then
        if service_pid_running "mcp_srv"; then
            echo -e "${GREEN}  ✓ mcp_srv 已在运行，跳过重复启动${NC}"
        elif port_listening "50053"; then
            MCP_PID=$(listening_pid_for_port "50053")
            if [ -n "${MCP_PID:-}" ]; then
                echo "$MCP_PID" > "$PID_DIR/mcp_srv.pid"
            fi
            echo -e "${YELLOW}  ⚠ 端口 50053 已被占用，跳过 mcp_srv 重复启动${NC}"
        else
            echo -e "  启动 mcp_srv..."
            nohup env GOCACHE="$GO_CACHE_DIR" go run ./backword_part/mcp_server/ > "$LOG_DIR/mcp_srv.log" 2>&1 &
            MCP_PID=$!
            echo $MCP_PID > "$PID_DIR/mcp_srv.pid"
            sleep 1
            MCP_LISTEN_PID=$(listening_pid_for_port "50053")
            if [ -n "${MCP_LISTEN_PID:-}" ]; then
                echo "$MCP_LISTEN_PID" > "$PID_DIR/mcp_srv.pid"
                MCP_PID="$MCP_LISTEN_PID"
            fi
            echo -e "${GREEN}    ✓ mcp_srv (PID: $MCP_PID)${NC}"
        fi
        sleep 1
    fi

    echo ""
}

# 启动 React 前端
start_frontend() {
    echo -e "${YELLOW}[5/7] 启动 React 前端...${NC}"

    if ! command -v npm &> /dev/null; then
        echo -e "${YELLOW}  跳过前端启动 (未安装 Node.js)${NC}"
        echo ""
        return
    fi

    if service_pid_running "frontend"; then
        echo -e "${GREEN}  ✓ React 前端已在运行，跳过重复启动${NC}"
        echo ""
        return
    fi

    cd "$PROJECT_ROOT/forward_part/static"

    # 检查是否已安装依赖
    if [ ! -d "node_modules" ]; then
        echo -e "  安装前端依赖..."
        npm install
    fi

    echo -e "  启动 Vite 开发服务器..."
    nohup npm run dev > "$LOG_DIR/frontend.log" 2>&1 &
    FRONTEND_PID=$!
    echo $FRONTEND_PID > "$PID_DIR/frontend.pid"

    sleep 2

    if ps -p $FRONTEND_PID > /dev/null; then
        echo -e "${GREEN}  ✓ React 前端已启动 (PID: $FRONTEND_PID)${NC}"
        echo -e "${GREEN}    访问: http://localhost:3000${NC}"
    else
        echo -e "${RED}  ✗ React 前端启动失败，查看日志: $LOG_DIR/frontend.log${NC}"
    fi

    cd "$PROJECT_ROOT"
    echo ""
}

# 显示服务状态
show_status() {
    echo -e "${YELLOW}[6/7] 检查服务状态...${NC}"

    sleep 3

    echo ""
    echo -e "${BLUE}服务状态:${NC}"
    echo -e "${BLUE}----------------------------------------${NC}"

    # 检查各个服务
    for service in $(runtime_services); do
        PID_FILE="$PID_DIR/${service}.pid"
        if [ -f "$PID_FILE" ]; then
            PID=$(cat "$PID_FILE")
            if ps -p $PID > /dev/null 2>&1; then
                echo -e "${GREEN}  ✓ $service (PID: $PID) - 运行中${NC}"
            else
                echo -e "${RED}  ✗ $service - 已停止${NC}"
            fi
        fi
    done

    # 检查 Nginx
    if [ "${NGINX_RUNTIME:-}" = "k8s" ]; then
        if k8s_openresty_running; then
            echo -e "${GREEN}  ✓ OpenResty/Nginx (K8s) - 运行中${NC}"
        else
            echo -e "${RED}  ✗ OpenResty/Nginx (K8s) - 未运行${NC}"
        fi
    elif [ "${NGINX_RUNTIME:-}" = "docker" ]; then
        if [ "$(docker inspect -f '{{.State.Running}}' "$DOCKER_OPENRESTY_CONTAINER" 2>/dev/null || true)" = "true" ]; then
            echo -e "${GREEN}  ✓ OpenResty/Nginx (Docker: $DOCKER_OPENRESTY_CONTAINER) - 运行中${NC}"
        fi
    elif [ ! -z "${NGINX_BIN:-}" ]; then
        NGINX_PID_FILE="$PROJECT_ROOT/forward_part/config/nginx/nginx.pid"
        if [ -f "$NGINX_PID_FILE" ]; then
            NGINX_PID=$(cat "$NGINX_PID_FILE")
            if ps -p $NGINX_PID > /dev/null 2>&1; then
                echo -e "${GREEN}  ✓ OpenResty/Nginx (PID: $NGINX_PID) - 运行中${NC}"
            fi
        fi
    fi

    if [ -x "$CLAMD_HELPER" ]; then
        "$CLAMD_HELPER" status 2>/dev/null | sed 's/^/  /'
    fi

    echo ""
}

# 显示访问信息
show_info() {
    echo -e "${YELLOW}[7/7] 启动完成！${NC}"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  访问地址:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "${GREEN}  前端:     http://localhost:3000${NC}"

    # 获取 Gateway 实际端口
    GATEWAY_PID_FILE="$PID_DIR/gateway.pid"
    if [ -f "$GATEWAY_PID_FILE" ]; then
        GATEWAY_PID=$(cat "$GATEWAY_PID_FILE")
        GATEWAY_PORT=$(lsof -i -P -n 2>/dev/null | grep $GATEWAY_PID | grep LISTEN | awk '{print $9}' | cut -d: -f2 | head -1)
        if [ ! -z "$GATEWAY_PORT" ]; then
            echo -e "${GREEN}  网关:     http://127.0.0.1:$GATEWAY_PORT${NC}"
        else
            echo -e "${GREEN}  网关:     (动态端口，查看日志)${NC}"
        fi
    fi

    echo -e "${GREEN}  反向代理: http://localhost:2024${NC}"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  Consul 服务发现:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "${GREEN}  http://127.0.0.1:30500/ui${NC}"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  日志文件:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "  $LOG_DIR/gateway.log"
    echo -e "  $LOG_DIR/outbox_relay.log"
    echo -e "  $LOG_DIR/storage_control.log"
    echo -e "  $LOG_DIR/store_srv.log"
    echo -e "  $LOG_DIR/frontend.log"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  实时查看日志:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "  tail -f $LOG_DIR/*.log"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  停止所有服务:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "  cd $PROJECT_ROOT/scripts/project_start_scripts"
    echo -e "  ./stop_all.sh"
    echo ""
}

# 主流程
main() {
    configure_core_feature_flags
    reconcile_k8s_runtime
    check_infrastructure
    check_dependencies
    start_nginx
    start_cpp_gateway
    start_go_services
    start_frontend
    show_status
    show_info
}

# 执行主流程
main
