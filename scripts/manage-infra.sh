#!/bin/bash

# CloudDisk V2 - 中间件管理脚本
# 用法: ./manage-infra.sh [start|stop|restart|status] [service_name]
# 示例:
#   ./manage-infra.sh start              # 启动所有服务
#   ./manage-infra.sh start mysql        # 只启动 MySQL
#   ./manage-infra.sh stop               # 停止所有服务
#   ./manage-infra.sh restart nacos      # 重启 Nacos
#   ./manage-infra.sh status             # 查看所有服务状态

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 服务列表（按依赖顺序）
SERVICES=(
    "namespace:namespace.yaml"
    "mysql:mysql-deployment.yaml"
    "redis:redis-deployment.yaml"
    "minio:minio.yaml"
    "consul:consul-statefulset.yaml"
    "zookeeper:kafka.yaml"
    "kafka:kafka.yaml"
    "nacos:nacos-deployment.yaml"
    "services:services.yaml"
)

# 服务显示名称映射
declare -A SERVICE_NAMES=(
    ["namespace"]="Namespace"
    ["mysql"]="MySQL"
    ["redis"]="Redis"
    ["minio"]="MinIO"
    ["consul"]="Consul"
    ["zookeeper"]="Zookeeper"
    ["kafka"]="Kafka"
    ["nacos"]="Nacos"
    ["services"]="Services"
)

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 显示帮助信息
show_help() {
    echo "=========================================="
    echo "CloudDisk V2 中间件管理脚本"
    echo "=========================================="
    echo ""
    echo "用法: $0 [命令] [服务名]"
    echo ""
    echo "命令:"
    echo "  start    - 启动服务"
    echo "  stop     - 停止服务"
    echo "  restart  - 重启服务"
    echo "  status   - 查看服务状态"
    echo "  logs     - 查看服务日志"
    echo "  help     - 显示帮助信息"
    echo ""
    echo "服务名 (可选):"
    echo "  all      - 所有服务 (默认)"
    echo "  mysql    - MySQL 数据库"
    echo "  redis    - Redis 缓存"
    echo "  minio    - MinIO 对象存储"
    echo "  nacos    - Nacos 配置中心"
    echo "  consul   - Consul 服务发现"
    echo "  kafka    - Kafka 消息队列"
    echo "  zookeeper- Zookeeper"
    echo ""
    echo "示例:"
    echo "  $0 start              # 启动所有服务"
    echo "  $0 start mysql        # 只启动 MySQL"
    echo "  $0 stop kafka         # 停止 Kafka"
    echo "  $0 restart nacos      # 重启 Nacos"
    echo "  $0 status             # 查看所有服务状态"
    echo "  $0 logs mysql         # 查看 MySQL 日志"
    echo ""
}

# 启动服务
start_service() {
    local service=$1
    local file=$2

    echo -e "${BLUE}[启动] ${SERVICE_NAMES[$service]}...${NC}"

    if [ ! -f "$file" ]; then
        echo -e "${RED}✗ 文件不存在: $file${NC}"
        return 1
    fi

    kubectl apply -f "$file" > /dev/null 2>&1

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ ${SERVICE_NAMES[$service]} 已启动${NC}"
        return 0
    else
        echo -e "${RED}✗ ${SERVICE_NAMES[$service]} 启动失败${NC}"
        return 1
    fi
}

# 停止服务
stop_service() {
    local service=$1
    local file=$2

    echo -e "${BLUE}[停止] ${SERVICE_NAMES[$service]}...${NC}"

    if [ ! -f "$file" ]; then
        echo -e "${RED}✗ 文件不存在: $file${NC}"
        return 1
    fi

    kubectl delete -f "$file" --ignore-not-found=true > /dev/null 2>&1

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓ ${SERVICE_NAMES[$service]} 已停止${NC}"
        return 0
    else
        echo -e "${RED}✗ ${SERVICE_NAMES[$service]} 停止失败${NC}"
        return 1
    fi
}

# 重启服务
restart_service() {
    local service=$1
    local file=$2

    echo -e "${BLUE}[重启] ${SERVICE_NAMES[$service]}...${NC}"

    stop_service "$service" "$file"
    sleep 3
    start_service "$service" "$file"
}

# 查看服务状态
show_status() {
    local service=$1

    echo "=========================================="
    echo "服务状态"
    echo "=========================================="
    echo ""

    if [ "$service" == "all" ] || [ -z "$service" ]; then
        # 显示所有 Pod
        kubectl get pods -n infra
        echo ""

        # 显示所有 Service
        echo "=========================================="
        echo "服务端口"
        echo "=========================================="
        kubectl get svc -n infra
        echo ""

        # 显示访问地址
        LOCAL_IP=$(hostname -I | awk '{print $1}')
        echo "=========================================="
        echo "访问地址 (本地 IP: ${LOCAL_IP})"
        echo "=========================================="
        echo "Nacos Console: http://${LOCAL_IP}:30848/nacos"
        echo "MinIO Console: http://${LOCAL_IP}:30901"
        echo "Consul UI:     http://${LOCAL_IP}:30500"
        echo "MySQL:         ${LOCAL_IP}:30306"
        echo "Redis:         ${LOCAL_IP}:30379"
        echo "Kafka:         ${LOCAL_IP}:31092"
        echo ""
    else
        # 显示特定服务
        kubectl get pods -n infra -l app=$service
        echo ""
        kubectl get svc -n infra -l app=$service 2>/dev/null || true
    fi
}

# 查看日志
show_logs() {
    local service=$1

    if [ -z "$service" ] || [ "$service" == "all" ]; then
        echo -e "${RED}请指定要查看日志的服务名${NC}"
        echo "示例: $0 logs mysql"
        return 1
    fi

    echo "=========================================="
    echo "查看 ${SERVICE_NAMES[$service]} 日志"
    echo "=========================================="
    echo ""

    POD=$(kubectl get pod -n infra -l app=$service -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)

    if [ -z "$POD" ]; then
        echo -e "${RED}✗ 未找到 ${SERVICE_NAMES[$service]} 的 Pod${NC}"
        return 1
    fi

    echo "Pod: $POD"
    echo "按 Ctrl+C 退出日志查看"
    echo ""

    kubectl logs -f -n infra "$POD"
}

# 启动所有服务
start_all() {
    echo "=========================================="
    echo "启动所有中间件服务"
    echo "=========================================="
    echo ""

    for item in "${SERVICES[@]}"; do
        IFS=':' read -r service file <<< "$item"

        # 跳过 namespace 和 services（它们不是实际的服务）
        if [ "$service" == "namespace" ]; then
            kubectl apply -f "$file" > /dev/null 2>&1
            echo -e "${GREEN}✓ Namespace 已创建${NC}"
            continue
        fi

        if [ "$service" == "services" ]; then
            continue
        fi

        start_service "$service" "$file"
        sleep 2
    done

    # 最后应用 services
    echo ""
    echo -e "${BLUE}[配置] 服务端口...${NC}"
    kubectl apply -f services.yaml > /dev/null 2>&1
    echo -e "${GREEN}✓ 服务端口已配置${NC}"

    echo ""
    echo "=========================================="
    echo "等待服务启动..."
    echo "=========================================="
    sleep 10

    show_status
}

# 停止所有服务
stop_all() {
    echo "=========================================="
    echo "停止所有中间件服务"
    echo "=========================================="
    echo ""

    # 反向停止（先停止依赖服务）
    for ((i=${#SERVICES[@]}-1; i>=0; i--)); do
        item="${SERVICES[$i]}"
        IFS=':' read -r service file <<< "$item"

        if [ "$service" == "namespace" ]; then
            continue
        fi

        stop_service "$service" "$file"
        sleep 1
    done

    echo ""
    echo -e "${GREEN}所有服务已停止${NC}"
}

# 重启所有服务
restart_all() {
    echo "=========================================="
    echo "重启所有中间件服务"
    echo "=========================================="
    echo ""

    stop_all
    echo ""
    sleep 5
    start_all
}

# 主函数
main() {
    local command=$1
    local service=${2:-all}

    # 检查 kubectl
    if ! command -v kubectl &> /dev/null; then
        echo -e "${RED}✗ kubectl 未安装${NC}"
        exit 1
    fi

    # 检查集群连接
    if ! kubectl get nodes &> /dev/null; then
        echo -e "${RED}✗ 无法连接到 Kubernetes 集群${NC}"
        exit 1
    fi

    case "$command" in
        start)
            if [ "$service" == "all" ]; then
                start_all
            else
                # 查找服务对应的文件
                found=false
                for item in "${SERVICES[@]}"; do
                    IFS=':' read -r svc file <<< "$item"
                    if [ "$svc" == "$service" ]; then
                        start_service "$service" "$file"
                        found=true
                        break
                    fi
                done

                if [ "$found" == false ]; then
                    echo -e "${RED}✗ 未知的服务: $service${NC}"
                    echo "运行 '$0 help' 查看可用服务"
                    exit 1
                fi
            fi
            ;;

        stop)
            if [ "$service" == "all" ]; then
                stop_all
            else
                found=false
                for item in "${SERVICES[@]}"; do
                    IFS=':' read -r svc file <<< "$item"
                    if [ "$svc" == "$service" ]; then
                        stop_service "$service" "$file"
                        found=true
                        break
                    fi
                done

                if [ "$found" == false ]; then
                    echo -e "${RED}✗ 未知的服务: $service${NC}"
                    exit 1
                fi
            fi
            ;;

        restart)
            if [ "$service" == "all" ]; then
                restart_all
            else
                found=false
                for item in "${SERVICES[@]}"; do
                    IFS=':' read -r svc file <<< "$item"
                    if [ "$svc" == "$service" ]; then
                        restart_service "$service" "$file"
                        found=true
                        break
                    fi
                done

                if [ "$found" == false ]; then
                    echo -e "${RED}✗ 未知的服务: $service${NC}"
                    exit 1
                fi
            fi
            ;;

        status)
            show_status "$service"
            ;;

        logs)
            show_logs "$service"
            ;;

        help|--help|-h)
            show_help
            ;;

        *)
            echo -e "${RED}✗ 未知命令: $command${NC}"
            echo ""
            show_help
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@"
