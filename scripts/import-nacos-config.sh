#!/bin/bash

# Nacos 配置导入脚本
# 当前环境优先直接写入 Nacos MySQL 元数据库，避免依赖 Nacos 3.x 已废弃的旧接口。

set -euo pipefail

echo "=========================================="
echo "Nacos 配置导入工具"
echo "=========================================="
echo ""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PROJECT_ROOT="/home/lihaoqian/project/clouddisk_v2"
CONFIG_FILE="${PROJECT_ROOT}/scripts/nacos-config/clouddisk.json"

NACOS_SERVER="${NACOS_SERVER:-127.0.0.1:30848}"
NAMESPACE_ID="${NAMESPACE_ID:-ce99961c-0fcf-4f4f-81d6-ac2183f24df1}"
NAMESPACE_NAME="${NAMESPACE_NAME:-clouddisk-dev}"
DATA_ID="${DATA_ID:-clouddisk.json}"
GROUP="${GROUP:-dev}"

MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-30306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
MYSQL_DATABASE="${MYSQL_DATABASE:-nacos_config}"

K8S_NAMESPACE="${K8S_NAMESPACE:-infra}"
MYSQL_SELECTOR="${MYSQL_SELECTOR:-app=mysql}"
MYSQL_CONTAINER="${MYSQL_CONTAINER:-mysql}"
NACOS_DEPLOYMENT="${NACOS_DEPLOYMENT:-nacos}"
RESTART_NACOS_AFTER_IMPORT="${RESTART_NACOS_AFTER_IMPORT:-1}"

confirm_import=true
if [[ "${1:-}" == "--yes" || "${AUTO_CONFIRM:-}" == "1" ]]; then
    confirm_import=false
fi

mysql_exec_mode() {
    if command -v mysql > /dev/null 2>&1; then
        echo "local"
        return
    fi

    if command -v kubectl > /dev/null 2>&1 && kubectl --v=0 get namespace "$K8S_NAMESPACE" > /dev/null 2>&1; then
        local mysql_pod
        mysql_pod=$(kubectl --v=0 get pod -n "$K8S_NAMESPACE" -l "$MYSQL_SELECTOR" -o jsonpath='{.items[0].metadata.name}' 2>/dev/null || true)
        if [ -n "$mysql_pod" ]; then
            echo "kubectl"
            return
        fi
    fi

    echo "none"
}

run_mysql_sql() {
    local sql="$1"
    local mode

    mode=$(mysql_exec_mode)
    case "$mode" in
        local)
            MYSQL_PWD="$MYSQL_PASSWORD" mysql \
                -h"$MYSQL_HOST" \
                -P"$MYSQL_PORT" \
                -u"$MYSQL_USER" \
                "$MYSQL_DATABASE" \
                -e "$sql"
            ;;
        kubectl)
            local mysql_pod
            mysql_pod=$(kubectl --v=0 get pod -n "$K8S_NAMESPACE" -l "$MYSQL_SELECTOR" -o jsonpath='{.items[0].metadata.name}')
            kubectl --v=0 exec -i -n "$K8S_NAMESPACE" "$mysql_pod" -c "$MYSQL_CONTAINER" -- sh -lc \
                "mysql -u'$MYSQL_USER' -p'$MYSQL_PASSWORD' '$MYSQL_DATABASE'" <<EOF
$sql
EOF
            ;;
        *)
            echo -e "${RED}✗ 未找到可用的 MySQL 客户端，也无法通过 kubectl 进入 MySQL Pod${NC}"
            exit 1
            ;;
    esac
}

if [ ! -f "$CONFIG_FILE" ]; then
    echo -e "${RED}✗ 配置文件不存在: $CONFIG_FILE${NC}"
    exit 1
fi

CONFIG_CONTENT=$(cat "$CONFIG_FILE")
CONFIG_B64=$(base64 -w0 "$CONFIG_FILE")
CONFIG_MD5=$(md5sum "$CONFIG_FILE" | awk '{print $1}')
NOW_MS=$(date +%s%3N)

echo -e "${BLUE}配置信息:${NC}"
echo "  Nacos Server:   $NACOS_SERVER"
echo "  Namespace ID:   $NAMESPACE_ID"
echo "  Namespace Name: $NAMESPACE_NAME"
echo "  Data ID:        $DATA_ID"
echo "  Group:          $GROUP"
echo "  Config File:    $CONFIG_FILE"
echo "  Import Mode:    $(mysql_exec_mode)"
echo ""

echo -e "${BLUE}配置内容预览:${NC}"
echo "$CONFIG_CONTENT" | head -20
echo ""

if [ "$confirm_import" = true ]; then
    read -p "确认导入配置到 Nacos MySQL? (y/n): " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "取消导入"
        exit 0
    fi
fi

echo ""
echo -e "${BLUE}正在导入配置...${NC}"

SQL=$(cat <<EOF
CREATE DATABASE IF NOT EXISTS \`${MYSQL_DATABASE}\` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE \`${MYSQL_DATABASE}\`;

SET @cfg = FROM_BASE64('${CONFIG_B64}');

INSERT INTO tenant_info (
    kp, tenant_id, tenant_name, tenant_desc, create_source, gmt_create, gmt_modified
) VALUES (
    '1',
    '${NAMESPACE_ID}',
    '${NAMESPACE_NAME}',
    'CloudDisk V2 local namespace',
    'import-nacos-config.sh',
    ${NOW_MS},
    ${NOW_MS}
) ON DUPLICATE KEY UPDATE
    tenant_name = VALUES(tenant_name),
    tenant_desc = VALUES(tenant_desc),
    gmt_modified = VALUES(gmt_modified);

INSERT INTO config_info (
    data_id, group_id, content, md5, src_user, src_ip, app_name, tenant_id, type
) VALUES (
    '${DATA_ID}',
    '${GROUP}',
    @cfg,
    '${CONFIG_MD5}',
    'import-script',
    '127.0.0.1',
    'clouddisk_v2',
    '${NAMESPACE_ID}',
    'json'
) ON DUPLICATE KEY UPDATE
    content = VALUES(content),
    md5 = VALUES(md5),
    src_user = VALUES(src_user),
    src_ip = VALUES(src_ip),
    app_name = VALUES(app_name),
    type = VALUES(type),
    gmt_modified = CURRENT_TIMESTAMP;
EOF
)

run_mysql_sql "$SQL"

echo -e "${GREEN}✓ 配置导入成功${NC}"
echo ""

if [ "$RESTART_NACOS_AFTER_IMPORT" = "1" ] && command -v kubectl > /dev/null 2>&1; then
    if kubectl --v=0 get deployment -n "$K8S_NAMESPACE" "$NACOS_DEPLOYMENT" > /dev/null 2>&1; then
        echo -e "${BLUE}重启 Nacos 以刷新内存配置缓存...${NC}"
        kubectl --v=0 rollout restart deployment/"$NACOS_DEPLOYMENT" -n "$K8S_NAMESPACE"
        kubectl --v=0 rollout status deployment/"$NACOS_DEPLOYMENT" -n "$K8S_NAMESPACE" --timeout=180s
        echo ""
    else
        echo -e "${YELLOW}⚠ 未找到 Nacos deployment: ${K8S_NAMESPACE}/${NACOS_DEPLOYMENT}，请手动重启 Nacos 使配置生效${NC}"
        echo ""
    fi
fi

echo -e "${BLUE}验证结果:${NC}"
run_mysql_sql "SELECT data_id, group_id, tenant_id, type FROM config_info WHERE data_id='${DATA_ID}' AND group_id='${GROUP}' AND tenant_id='${NAMESPACE_ID}';"
echo ""
echo "后续建议:"
echo "  1. 确认 Nacos 服务已启动: http://${NACOS_SERVER}/nacos/"
echo "  2. 重新启动依赖 Nacos 的 Go 服务"
echo "  3. 如需跳过自动重启 Nacos，可设置: RESTART_NACOS_AFTER_IMPORT=0"
echo "  4. 如需免交互导入，可执行: $0 --yes"
