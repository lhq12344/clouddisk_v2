#!/bin/bash

# CloudDisk V2 停止所有服务脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

# PID 文件目录
PID_DIR="$PROJECT_ROOT/.pids"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CloudDisk V2 停止所有服务${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

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
            echo -e "${YELLOW}$service_name 未运行${NC}"
            rm -f "$pid_file"
        fi
    else
        echo -e "${YELLOW}$service_name 未找到 PID 文件${NC}"
    fi
}

# 停止 OpenResty/Nginx
stop_nginx() {
    NGINX_PID_FILE="$PROJECT_ROOT/forward_part/config/nginx/nginx.pid"

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
            echo -e "${YELLOW}OpenResty/Nginx 未运行${NC}"
        fi
        rm -f "$NGINX_PID_FILE"
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
    if [ -d "$PID_DIR" ]; then
        rm -rf "$PID_DIR"
    fi
}

# 执行主流程
main
