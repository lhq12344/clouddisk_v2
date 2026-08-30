#!/bin/bash

# MCP API Key 功能测试脚本

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

BASE_URL="http://localhost:8081"
USER_ID=1001
API_KEY=""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}MCP API Key 功能测试${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 1. 测试 Health Check
echo -e "${YELLOW}测试 1: Health Check${NC}"
if curl -s "$BASE_URL/health" | grep -q "ok"; then
    echo -e "${GREEN}✓ Health check 成功${NC}"
else
    echo -e "${RED}✗ Health check 失败${NC}"
    exit 1
fi
echo ""

# 2. 生成 API Key
echo -e "${YELLOW}测试 2: 生成 API Key${NC}"
RESPONSE=$(curl -s -X POST "$BASE_URL/api/apikey/generate" \
    -H "Content-Type: application/json" \
    -d "{\"user_id\": $USER_ID, \"name\": \"Test Key\", \"expiry_days\": 365}")

echo "响应: $RESPONSE"

API_KEY=$(echo "$RESPONSE" | grep -o '"api_key":"[^"]*"' | cut -d'"' -f4)

if [ -z "$API_KEY" ]; then
    echo -e "${RED}✗ 生成 API Key 失败${NC}"
    exit 1
fi

echo -e "${GREEN}✓ API Key 生成成功: ${BLUE}$API_KEY${NC}"
echo ""

# 3. 查询 API Key
echo -e "${YELLOW}测试 3: 查询 API Key${NC}"
RESPONSE=$(curl -s "$BASE_URL/api/apikey/get?user_id=$USER_ID")
echo "响应: $RESPONSE"

if echo "$RESPONSE" | grep -q "key_prefix"; then
    echo -e "${GREEN}✓ 查询 API Key 成功${NC}"
else
    echo -e "${RED}✗ 查询 API Key 失败${NC}"
    exit 1
fi
echo ""

# 4. 使用 API Key 访问受保护的接口
echo -e "${YELLOW}测试 4: 使用 API Key 访问 /api/apikey/me${NC}"
RESPONSE=$(curl -s "$BASE_URL/api/apikey/me" \
    -H "X-API-Key: $API_KEY")
echo "响应: $RESPONSE"

if echo "$RESPONSE" | grep -q "user_id"; then
    echo -e "${GREEN}✓ API Key 认证成功${NC}"
else
    echo -e "${RED}✗ API Key 认证失败${NC}"
    exit 1
fi
echo ""

# 5. 测试无效 API Key
echo -e "${YELLOW}测试 5: 测试无效 API Key${NC}"
RESPONSE=$(curl -s "$BASE_URL/api/apikey/me" \
    -H "X-API-Key: invalid_key_12345")
echo "响应: $RESPONSE"

if echo "$RESPONSE" | grep -q "error"; then
    echo -e "${GREEN}✓ 无效 API Key 被正确拒绝${NC}"
else
    echo -e "${RED}✗ 无效 API Key 未被拒绝${NC}"
    exit 1
fi
echo ""

# 6. 测试 Rate Limiting（发送 65 次请求）
echo -e "${YELLOW}测试 6: 测试 Rate Limiting（发送 65 次请求）${NC}"
echo "这可能需要几秒钟..."

SUCCESS_COUNT=0
RATE_LIMITED_COUNT=0

for i in {1..65}; do
    STATUS=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/apikey/me" \
        -H "X-API-Key: $API_KEY")

    if [ "$STATUS" = "200" ]; then
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
    elif [ "$STATUS" = "429" ]; then
        RATE_LIMITED_COUNT=$((RATE_LIMITED_COUNT + 1))
    fi

    # 每 10 次显示进度
    if [ $((i % 10)) -eq 0 ]; then
        echo "  进度: $i/65 (成功: $SUCCESS_COUNT, 限流: $RATE_LIMITED_COUNT)"
    fi
done

echo "最终结果: 成功 $SUCCESS_COUNT 次, 被限流 $RATE_LIMITED_COUNT 次"

if [ $RATE_LIMITED_COUNT -gt 0 ]; then
    echo -e "${GREEN}✓ Rate Limiting 正常工作${NC}"
else
    echo -e "${YELLOW}⚠ Rate Limiting 未触发（可能需要更多请求）${NC}"
fi
echo ""

# 7. 撤销 API Key
echo -e "${YELLOW}测试 7: 撤销 API Key${NC}"
RESPONSE=$(curl -s -X POST "$BASE_URL/api/apikey/revoke" \
    -H "Content-Type: application/json" \
    -d "{\"user_id\": $USER_ID}")
echo "响应: $RESPONSE"

if echo "$RESPONSE" | grep -q "success"; then
    echo -e "${GREEN}✓ API Key 撤销成功${NC}"
else
    echo -e "${RED}✗ API Key 撤销失败${NC}"
    exit 1
fi
echo ""

# 8. 验证撤销后无法使用
echo -e "${YELLOW}测试 8: 验证撤销后的 API Key 无法使用${NC}"
RESPONSE=$(curl -s "$BASE_URL/api/apikey/me" \
    -H "X-API-Key: $API_KEY")
echo "响应: $RESPONSE"

if echo "$RESPONSE" | grep -q "error"; then
    echo -e "${GREEN}✓ 撤销后的 API Key 被正确拒绝${NC}"
else
    echo -e "${RED}✗ 撤销后的 API Key 仍然可用${NC}"
    exit 1
fi
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}所有测试通过！${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "数据库验证命令:"
echo "  mysql -u root -p clouddisk -e 'SELECT * FROM mcp_api_keys WHERE user_id = $USER_ID;'"
echo "  mysql -u root -p clouddisk -e 'SELECT * FROM mcp_api_key_logs ORDER BY request_time DESC LIMIT 10;'"
echo "  mysql -u root -p clouddisk -e 'SELECT * FROM mcp_rate_limits ORDER BY window_start DESC LIMIT 10;'"
echo ""
