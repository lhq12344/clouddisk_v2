#!/bin/bash

set -euo pipefail

UNIFIED_SCRIPT="/home/lihaoqian/project/k8s/bin/k8s-stack.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

declare -A SERVICE_NAMES=(
    ["mysql"]="MySQL"
    ["redis"]="Redis"
    ["minio"]="MinIO"
    ["consul"]="Consul"
    ["zookeeper"]="Zookeeper"
    ["kafka"]="Kafka"
    ["nacos"]="Nacos"
)

show_help() {
    cat <<EOF
CloudDisk V2 的 K8s YAML 已迁移到统一目录：
  /home/lihaoqian/project/k8s

统一目录按组件名组织，不再按项目建目录。

当前脚本只保留：
  $0 status [service]
  $0 logs <service>

统一脚本示例：
  ${UNIFIED_SCRIPT} deploy mysql
  ${UNIFIED_SCRIPT} start redis
  ${UNIFIED_SCRIPT} stop nacos
  ${UNIFIED_SCRIPT} status clouddisk_v2
EOF
}

ensure_unified_script() {
    if [ ! -x "${UNIFIED_SCRIPT}" ]; then
        echo -e "${RED}✗ 未找到统一脚本: ${UNIFIED_SCRIPT}${NC}"
        exit 1
    fi
}

show_status() {
    local service=${1:-all}
    ensure_unified_script

    if [ "${service}" = "all" ]; then
        exec "${UNIFIED_SCRIPT}" status clouddisk_v2
    fi

    exec "${UNIFIED_SCRIPT}" status "${service}"
}

show_logs() {
    local service=${1:-}

    if [ -z "${service}" ] || [ "${service}" = "all" ]; then
        echo -e "${RED}请指定要查看日志的服务名${NC}"
        echo "示例: $0 logs mysql"
        exit 1
    fi

    local pod
    pod=$(kubectl get pod -n infra -l "app=${service}" -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)
    if [ -z "${pod}" ]; then
        echo -e "${RED}✗ 未找到 ${SERVICE_NAMES[$service]:-$service} 的 Pod${NC}"
        exit 1
    fi

    echo -e "${GREEN}查看 ${SERVICE_NAMES[$service]:-$service} 日志: ${pod}${NC}"
    kubectl logs -f -n infra "${pod}"
}

show_migration_tip() {
    echo -e "${YELLOW}K8s Pod 生命周期管理已迁移到统一脚本：${UNIFIED_SCRIPT}${NC}"
    echo -e "${YELLOW}请改用：${UNIFIED_SCRIPT} deploy|start|stop <组件名>（clouddisk_v2 仍可作为兼容别名）${NC}"
}

main() {
    local command=${1:-help}
    local service=${2:-all}

    case "${command}" in
        status)
            show_status "${service}"
            ;;
        logs)
            show_logs "${service}"
            ;;
        start|stop|restart)
            show_migration_tip
            exit 1
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            show_help
            exit 1
            ;;
    esac
}

main "$@"
