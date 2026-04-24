#!/bin/bash

# CloudDisk V2 停止所有服务脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 项目根目录 - 修复为正确路径
PROJECT_ROOT="/home/lihaoqian/project/clouddisk_v2"
cd "$PROJECT_ROOT"

# PID 文件目录
PID_DIR="$PROJECT_ROOT/.pids"
NGINX_MODE_FILE="$PID_DIR/nginx.mode"
DOCKER_OPENRESTY_CONTAINER="${DOCKER_OPENRESTY_CONTAINER:-clouddisk_v2_openresty}"
CLAMD_HELPER="$PROJECT_ROOT/scripts/project_start_scripts/clamd_local.sh"
K8S_NAMESPACE="infra"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CloudDisk V2 停止所有服务${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

k8s_openresty_running() {
    command -v kubectl &> /dev/null || return 1

    kubectl get pods -n "$K8S_NAMESPACE" -l app=openresty --no-headers 2>/dev/null \
        | awk '{split($2, ready, "/"); if (ready[1] == ready[2] && $3 == "Running") found=1} END {exit found ? 0 : 1}'
}

# 停止服务函数
stop_service() {
    local service_name=$1
    local pid_file="$PID_DIR/${service_name}.pid"

    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if ps -p $pid > /dev/null 2>&1; then
            echo -e "${YELLOW}停止 $service_name (PID: $pid)...${NC}"
            kill $pid 2>/dev/null || kill -9 $pid 2>/dev/null
            sleep 1
            if ps -p $pid > /dev/null 2>&1; then
                echo -e "${RED}  ✗ 无法停止 $service_name${NC}"
            else
                echo -e "${GREEN}  ✓ $service_name 已停止${NC}"
                rm -f "$pid_file"
            fi
        else
            echo -e "${YELLOW}$service_name 未运行 (清理 PID 文件)${NC}"
            rm -f "$pid_file"
        fi
    else
        echo -e "${YELLOW}$service_name 未找到 PID 文件${NC}"
    fi
}

# 停止 OpenResty/Nginx
stop_nginx() {
    NGINX_PID_FILE="$PROJECT_ROOT/forward_part/config/nginx/nginx.pid"

    if k8s_openresty_running; then
        echo -e "${YELLOW}OpenResty/Nginx 由 K8s 接管，跳过本地停止${NC}"
        rm -f "$NGINX_MODE_FILE"
        return
    fi

    if [ -f "$NGINX_MODE_FILE" ] && [ "$(cat "$NGINX_MODE_FILE" 2>/dev/null)" = "docker" ]; then
        if command -v docker &> /dev/null && docker inspect "$DOCKER_OPENRESTY_CONTAINER" > /dev/null 2>&1; then
            echo -e "${YELLOW}停止 OpenResty/Nginx Docker 容器 (${DOCKER_OPENRESTY_CONTAINER})...${NC}"
            docker rm -f "$DOCKER_OPENRESTY_CONTAINER" > /dev/null 2>&1 || true
            echo -e "${GREEN}  ✓ OpenResty/Nginx Docker 容器已停止${NC}"
        else
            echo -e "${YELLOW}OpenResty/Nginx Docker 容器未运行${NC}"
        fi
        rm -f "$NGINX_MODE_FILE" "$NGINX_PID_FILE"
        return
    fi

    if [ -f "$NGINX_PID_FILE" ]; then
        NGINX_PID=$(cat "$NGINX_PID_FILE")
        if ps -p $NGINX_PID > /dev/null 2>&1; then
            echo -e "${YELLOW}停止 OpenResty/Nginx (PID: $NGINX_PID)...${NC}"

            # 尝试使用 nginx -s stop
            if command -v openresty &> /dev/null; then
                openresty -s stop -c "$PROJECT_ROOT/forward_part/config/nginx/nginx.conf" 2>/dev/null || kill $NGINX_PID
            elif command -v nginx &> /dev/null; then
                nginx -s stop -c "$PROJECT_ROOT/forward_part/config/nginx/nginx.conf" 2>/dev/null || kill $NGINX_PID
            else
                kill $NGINX_PID
            fi

            sleep 1
            echo -e "${GREEN}  ✓ OpenResty/Nginx 已停止${NC}"
        else
            echo -e "${YELLOW}OpenResty/Nginx 未运行 (清理 PID 文件)${NC}"
        fi
        rm -f "$NGINX_PID_FILE" "$NGINX_MODE_FILE"
    else
        echo -e "${YELLOW}OpenResty/Nginx 未找到 PID 文件${NC}"
    fi
}

# 主流程
main() {
    echo -e "${YELLOW}正在停止所有服务...${NC}"
    echo ""

    # 停止前端
    stop_service "frontend"

    # 停止 Go 微服务
    stop_service "mcp_srv"
    stop_service "ai_srv"
    stop_service "store_srv"
    stop_service "file_srv"
    stop_service "account_srv"
    if [ -x "$CLAMD_HELPER" ]; then
        echo -e "${YELLOW}停止 clamd_local...${NC}"
        "$CLAMD_HELPER" stop | sed 's/^/  /'
    fi

    # 停止 C++ Gateway
    stop_service "gateway"

    # 停止 Nginx
    stop_nginx

    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  所有服务已停止${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""

    # 清理 PID 目录
    if [ -d "$PID_DIR" ] && [ -z "$(ls -A $PID_DIR)" ]; then
        echo -e "${BLUE}清理空的 PID 目录...${NC}"
        rm -rf "$PID_DIR"
    fi
}

# 执行主流程
main
