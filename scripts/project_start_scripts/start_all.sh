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
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

# 日志目录
LOG_DIR="$PROJECT_ROOT/log"
mkdir -p "$LOG_DIR"

# PID 文件目录
PID_DIR="$PROJECT_ROOT/.pids"
mkdir -p "$PID_DIR"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  CloudDisk V2 一键启动脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

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
    if command -v openresty &> /dev/null; then
        NGINX_BIN="openresty"
        echo -e "${GREEN}  ✓ OpenResty: $(openresty -v 2>&1 | head -1)${NC}"
    elif command -v nginx &> /dev/null; then
        NGINX_BIN="nginx"
        echo -e "${GREEN}  ✓ Nginx: $(nginx -v 2>&1)${NC}"
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
    if [ -z "$NGINX_BIN" ]; then
        echo -e "${YELLOW}[2/7] 跳过 OpenResty/Nginx 启动${NC}"
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

    # 启动 Nginx
    $NGINX_BIN -c "$NGINX_CONF" -p "$PROJECT_ROOT/forward_part/config/nginx/"

    if [ $? -eq 0 ]; then
        echo -e "${GREEN}  ✓ OpenResty/Nginx 已启动${NC}"
    else
        echo -e "${RED}  ✗ OpenResty/Nginx 启动失败${NC}"
    fi

    echo ""
}

# 启动 C++ Gateway
start_cpp_gateway() {
    echo -e "${YELLOW}[3/7] 启动 C++ Gateway...${NC}"

    GATEWAY_BIN="$PROJECT_ROOT/forward_part/gateway/build/gateway"

    if [ ! -f "$GATEWAY_BIN" ]; then
        echo -e "${RED}  ✗ 未找到 Gateway 二进制文件${NC}"
        echo ""
        return
    fi

    cd "$PROJECT_ROOT/forward_part/gateway"

    nohup ./build/gateway > "$LOG_DIR/gateway.log" 2>&1 &
    GATEWAY_PID=$!
    echo $GATEWAY_PID > "$PID_DIR/gateway.pid"

    sleep 1

    if ps -p $GATEWAY_PID > /dev/null; then
        echo -e "${GREEN}  ✓ C++ Gateway 已启动 (PID: $GATEWAY_PID)${NC}"
    else
        echo -e "${RED}  ✗ C++ Gateway 启动失败，查看日志: $LOG_DIR/gateway.log${NC}"
    fi

    cd "$PROJECT_ROOT"
    echo ""
}

# 启动 Go 微服务
start_go_services() {
    echo -e "${YELLOW}[4/7] 启动 Go 微服务...${NC}"

    # Account Service
    echo -e "  启动 account_srv..."
    cd "$PROJECT_ROOT"
    nohup go run ./backword_part/account_server/account_srv/ > "$LOG_DIR/account_srv.log" 2>&1 &
    ACCOUNT_PID=$!
    echo $ACCOUNT_PID > "$PID_DIR/account_srv.pid"
    echo -e "${GREEN}    ✓ account_srv (PID: $ACCOUNT_PID)${NC}"

    sleep 2

    # File Service
    echo -e "  启动 file_srv..."
    nohup go run ./backword_part/file_server/file_srv/ > "$LOG_DIR/file_srv.log" 2>&1 &
    FILE_PID=$!
    echo $FILE_PID > "$PID_DIR/file_srv.pid"
    echo -e "${GREEN}    ✓ file_srv (PID: $FILE_PID)${NC}"

    sleep 2

    # Store Service
    echo -e "  启动 store_srv..."
    nohup go run ./other_srv/store_srv/ > "$LOG_DIR/store_srv.log" 2>&1 &
    STORE_PID=$!
    echo $STORE_PID > "$PID_DIR/store_srv.pid"
    echo -e "${GREEN}    ✓ store_srv (PID: $STORE_PID)${NC}"

    sleep 2

    # AI Service (可选)
    if [ -f "./backword_part/AI_server/main.go" ]; then
        echo -e "  启动 AI_srv..."
        nohup go run ./backword_part/AI_server/ > "$LOG_DIR/ai_srv.log" 2>&1 &
        AI_PID=$!
        echo $AI_PID > "$PID_DIR/ai_srv.pid"
        echo -e "${GREEN}    ✓ AI_srv (PID: $AI_PID)${NC}"
        sleep 1
    fi

    # MCP Service (可选)
    if [ -f "./backword_part/mcp_server/main.go" ]; then
        echo -e "  启动 mcp_srv..."
        nohup go run ./backword_part/mcp_server/ > "$LOG_DIR/mcp_srv.log" 2>&1 &
        MCP_PID=$!
        echo $MCP_PID > "$PID_DIR/mcp_srv.pid"
        echo -e "${GREEN}    ✓ mcp_srv (PID: $MCP_PID)${NC}"
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
    for service in gateway account_srv file_srv store_srv ai_srv mcp_srv frontend; do
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
    if [ ! -z "$NGINX_BIN" ]; then
        NGINX_PID_FILE="$PROJECT_ROOT/forward_part/config/nginx/nginx.pid"
        if [ -f "$NGINX_PID_FILE" ]; then
            NGINX_PID=$(cat "$NGINX_PID_FILE")
            if ps -p $NGINX_PID > /dev/null 2>&1; then
                echo -e "${GREEN}  ✓ OpenResty/Nginx (PID: $NGINX_PID) - 运行中${NC}"
            fi
        fi
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
    echo -e "${GREEN}  网关:     http://localhost:8080${NC}"
    echo -e "${GREEN}  反向代理: http://localhost:2024${NC}"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  日志文件:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "  $LOG_DIR/gateway.log"
    echo -e "  $LOG_DIR/account_srv.log"
    echo -e "  $LOG_DIR/file_srv.log"
    echo -e "  $LOG_DIR/store_srv.log"
    echo -e "  $LOG_DIR/frontend.log"
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}  停止所有服务:${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo -e "  ./stop_all.sh"
    echo ""
}

# 主流程
main() {
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
