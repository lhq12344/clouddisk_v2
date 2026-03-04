#!/bin/bash

# CloudDisk V2 查看服务状态脚本

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
LOG_DIR="$PROJECT_ROOT/log"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CloudDisk V2 服务状态${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# 检查服务状态
check_service() {
    local service_name=$1
    local pid_file="$PID_DIR/${service_name}.pid"
    local log_file="$LOG_DIR/${service_name}.log"

    if [ -f "$pid_file" ]; then
        local pid=$(cat "$pid_file")
        if ps -p $pid > /dev/null 2>&1; then
            # 获取内存使用
            local mem=$(ps -p $pid -o rss= | awk '{printf "%.1f MB", $1/1024}')
            # 获取 CPU 使用
            local cpu=$(ps -p $pid -o %cpu= | awk '{print $1"%"}')
            # 获取运行时间
            local uptime=$(ps -p $pid -o etime= | awk '{print $1}')

            echo -e "${GREEN}✓ $service_name${NC}"
            echo -e "  PID:    $pid"
            echo -e "  状态:   运行中"
            echo -e "  内存:   $mem"
            echo -e "  CPU:    $cpu"
            echo -e "  运行:   $uptime"

            # 显示最后几行日志
            if [ -f "$log_file" ]; then
                echo -e "  日志:   $log_file"
                echo -e "${YELLOW}  最近日志:${NC}"
                tail -n 3 "$log_file" 2>/dev/null | sed 's/^/    /'
            fi
            echo ""
        else
            echo -e "${RED}✗ $service_name${NC}"
            echo -e "  状态:   已停止 (PID 文件存在但进程不存在)"
            echo ""
        fi
    else
        echo -e "${YELLOW}○ $service_name${NC}"
        echo -e "  状态:   未启动"
        echo ""
    fi
}

# 检查 Nginx
check_nginx() {
    NGINX_PID_FILE="$PROJECT_ROOT/forward_part/config/nginx/nginx.pid"

    if [ -f "$NGINX_PID_FILE" ]; then
        NGINX_PID=$(cat "$NGINX_PID_FILE")
        if ps -p $NGINX_PID > /dev/null 2>&1; then
            local mem=$(ps -p $NGINX_PID -o rss= | awk '{printf "%.1f MB", $1/1024}')
            local uptime=$(ps -p $NGINX_PID -o etime= | awk '{print $1}')

            echo -e "${GREEN}✓ OpenResty/Nginx${NC}"
            echo -e "  PID:    $NGINX_PID"
            echo -e "  状态:   运行中"
            echo -e "  内存:   $mem"
            echo -e "  运行:   $uptime"
            echo ""
        else
            echo -e "${RED}✗ OpenResty/Nginx${NC}"
            echo -e "  状态:   已停止"
            echo ""
        fi
    else
        echo -e "${YELLOW}○ OpenResty/Nginx${NC}"
        echo -e "  状态:   未启动"
        echo ""
    fi
}

# 主流程
main() {
    # 检查 Nginx
    check_nginx

    # 检查 C++ Gateway
    check_service "gateway"

    # 检查 Go 微服务
    check_service "account_srv"
    check_service "file_srv"
    check_service "store_srv"
    check_service "ai_srv"
    check_service "mcp_srv"

    # 检查前端
    check_service "frontend"

    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  端口监听情况${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""

    # 检查常用端口
    for port in 2024 3000 8080 50051 50052; do
        if netstat -tuln 2>/dev/null | grep -q ":$port "; then
            echo -e "${GREEN}  ✓ 端口 $port 正在监听${NC}"
        else
            echo -e "${YELLOW}  ○ 端口 $port 未监听${NC}"
        fi
    done

    echo ""
}

# 执行主流程
main
