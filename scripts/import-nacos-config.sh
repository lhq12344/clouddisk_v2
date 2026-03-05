#!/bin/bash

# Nacos 配置导入脚本
# 执行方式: chmod +x import-nacos-config.sh && ./import-nacos-config.sh

set -e

echo "=========================================="
echo "Nacos 配置导入工具"
echo "=========================================="
echo ""

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 配置参数
NACOS_SERVER="172.20.10.3:30848"
NACOS_USERNAME="nacos"
NACOS_PASSWORD="nacos"
NAMESPACE_ID="ce99961c-0fcf-4f4f-81d6-ac2183f24df1"  # 需要先在 Nacos 创建
DATA_ID="clouddisk.json"
GROUP="dev"
CONFIG_FILE="./nacos-config/clouddisk.json"

# 检查配置文件
if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}✗ 配置文件不存在: $CONFIG_FILE${NC}"
    exit 1
fi

echo -e "${BLUE}配置信息:${NC}"
echo "  Nacos Server: $NACOS_SERVER"
echo "  Namespace ID: $NAMESPACE_ID"
echo "  Data ID:      $DATA_ID"
echo "  Group:        $GROUP"
echo "  Config File:  $CONFIG_FILE"
echo ""

# 读取配置文件内容
CONFIG_CONTENT=$(cat "$CONFIG_FILE")

echo -e "${BLUE}配置内容预览:${NC}"
echo "$CONFIG_CONTENT" | head -20
echo ""

# 确认导入
read -p "确认导入配置到 Nacos? (y/n): " -n 1 -r
echo ""

if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "取消导入"
    exit 0
fi

echo ""
echo -e "${BLUE}正在导入配置...${NC}"

# 1. 先登录获取 token
LOGIN_RESPONSE=$(curl -s -X POST "http://${NACOS_SERVER}/nacos/v1/auth/login" \
  -d "username=${NACOS_USERNAME}" \
  -d "password=${NACOS_PASSWORD}")

ACCESS_TOKEN=$(echo "$LOGIN_RESPONSE" | grep -o '"accessToken":"[^"]*"' | cut -d'"' -f4)

if [ -z "$ACCESS_TOKEN" ]; then
    echo -e "${RED}✗ 登录失败${NC}"
    echo "响应: $LOGIN_RESPONSE"
    exit 1
fi

echo -e "${GREEN}✓ 登录成功，获取到 token${NC}"

# 2. 使用 token 导入配置
RESPONSE=$(curl -s -X POST "http://${NACOS_SERVER}/nacos/v1/cs/configs" \
  -d "dataId=${DATA_ID}" \
  -d "group=${GROUP}" \
  -d "tenant=${NAMESPACE_ID}" \
  -d "content=${CONFIG_CONTENT}" \
  -d "type=json" \
  -H "accessToken: ${ACCESS_TOKEN}")

if [ "$RESPONSE" == "true" ]; then
    echo -e "${GREEN}✓ 配置导入成功！${NC}"
    echo ""
    echo "访问 Nacos Console 查看配置:"
    echo "  http://${NACOS_SERVER}/nacos"
    echo "  命名空间: $NAMESPACE_ID"
    echo "  Data ID: $DATA_ID"
    echo "  Group: $GROUP"
else
    echo -e "${RED}✗ 配置导入失败${NC}"
    echo "响应: $RESPONSE"
    echo ""
    echo "可能的原因:"
    echo "1. Namespace ID 不存在（需要先在 Nacos Console 创建）"
    echo "2. 用户名密码错误"
    echo "3. Nacos 服务未启动"
    exit 1
fi

echo ""
echo "=========================================="
echo "后续步骤"
echo "=========================================="
echo ""
echo "1. 在 Nacos Console 中验证配置:"
echo "   http://${NACOS_SERVER}/nacos"
echo ""
echo "2. 修改服务代码中的 Nacos 配置:"
echo "   - Nacos Server: ${NACOS_SERVER}"
echo "   - Namespace ID: ${NAMESPACE_ID}"
echo ""
echo "3. 启动服务测试配置是否生效"
echo ""
