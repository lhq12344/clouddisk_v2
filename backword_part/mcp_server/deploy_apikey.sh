#!/bin/bash

# MCP API Key 认证系统部署脚本
# 使用方法: ./deploy_apikey.sh [mysql_password]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}MCP API Key 认证系统部署脚本${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 1. 检查 MySQL 密码
MYSQL_PASSWORD="${1:-123456}"
echo -e "${YELLOW}步骤 1/6: 检查数据库连接...${NC}"
if ! mysql -u root -p"$MYSQL_PASSWORD" -e "SELECT 1" &>/dev/null; then
    echo -e "${RED}✗ 数据库连接失败！请检查 MySQL 密码${NC}"
    echo "使用方法: ./deploy_apikey.sh [mysql_password]"
    exit 1
fi
echo -e "${GREEN}✓ 数据库连接成功${NC}"
echo ""

# 2. 更新 migrate.go 中的数据库密码
echo -e "${YELLOW}步骤 2/6: 更新数据库配置...${NC}"
sed -i "s/root:.*@tcp/root:$MYSQL_PASSWORD@tcp/" migrate.go
echo -e "${GREEN}✓ 数据库配置已更新${NC}"
echo ""

# 3. 运行数据库迁移
echo -e "${YELLOW}步骤 3/6: 运行数据库迁移...${NC}"
export GOROOT=/home/lihaoqian/go
export PATH=$GOROOT/bin:$PATH
export GOPATH=/home/lihaoqian/gopath

if go run migrate.go; then
    echo -e "${GREEN}✓ 数据库迁移成功${NC}"
else
    echo -e "${RED}✗ 数据库迁移失败${NC}"
    exit 1
fi
echo ""

# 4. 备份原 main.go
echo -e "${YELLOW}步骤 4/6: 备份原 main.go...${NC}"
if [ -f "main.go" ] && [ ! -f "main.go.bak" ]; then
    cp main.go main.go.bak
    echo -e "${GREEN}✓ 已备份到 main.go.bak${NC}"
else
    echo -e "${YELLOW}⚠ 跳过备份（已存在 main.go.bak）${NC}"
fi
echo ""

# 5. 替换 main.go
echo -e "${YELLOW}步骤 5/6: 替换 main.go...${NC}"
if [ -f "main_with_apikey.go" ]; then
    cp main_with_apikey.go main.go
    echo -e "${GREEN}✓ main.go 已更新为 API Key 版本${NC}"
else
    echo -e "${RED}✗ main_with_apikey.go 不存在${NC}"
    exit 1
fi
echo ""

# 6. 编译项目
echo -e "${YELLOW}步骤 6/6: 编译项目...${NC}"
go mod tidy
if go build -o mcp_server .; then
    echo -e "${GREEN}✓ 编译成功${NC}"
else
    echo -e "${RED}✗ 编译失败${NC}"
    exit 1
fi
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}部署完成！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "下一步操作："
echo "1. 启动服务: ./mcp_server"
echo "2. 测试生成 API Key:"
echo "   curl -X POST http://localhost:8081/api/apikey/generate \\"
echo "     -H 'Content-Type: application/json' \\"
echo "     -d '{\"user_id\": 1001, \"name\": \"Test Key\", \"expiry_days\": 365}'"
echo ""
echo "如需回滚到旧版本:"
echo "   cp main.go.bak main.go && go build -o mcp_server ."
echo ""
